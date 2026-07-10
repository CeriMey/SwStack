#pragma once

/**
 * @file src/media/SwMp4Demuxer.h
 * @ingroup media
 * @brief Declares a dependency-free ISO BMFF (MP4/MOV/M4V) demuxer.
 *
 * The demuxer parses the `moov` sample tables of a progressive (non-fragmented) MP4 file and
 * exposes per-track random access to samples: decode/presentation timestamps in milliseconds,
 * key-frame flags and payloads. H.264/H.265 samples are converted from length-prefixed NAL
 * units to Annex-B byte streams; the codec parameter sets (`avcC`/`hvcC`) are exposed as an
 * Annex-B blob ready to prepend on key frames. AV1 samples are exposed as raw OBU streams
 * with the `av1C` config OBUs as key-frame prefix.
 *
 * Platform-neutral and header-only, in line with the native MPEG-TS demuxer
 * (`media/rtp/SwTsProgramDemux.h`). Fragmented MP4 (`moof`) and edit lists are out of scope:
 * fragmented files are rejected with a clear error, edit lists are ignored.
 */

#include "SwDebug.h"
#include "core/types/SwByteArray.h"
#include "media/SwAudioPacket.h"
#include "media/SwHevcBitstream.h"
#include "media/SwVideoPacket.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static constexpr const char* kSwLogCategory_SwMp4Demuxer = "sw.media.swmp4demuxer";

class SwMp4Demuxer {
public:
    enum class TrackKind {
        Unknown,
        Video,
        Audio
    };

    struct Track {
        uint32_t trackId{0};
        TrackKind kind{TrackKind::Unknown};
        SwVideoPacket::Codec videoCodec{SwVideoPacket::Codec::Unknown};
        SwAudioPacket::Codec audioCodec{SwAudioPacket::Codec::Unknown};
        std::string codecName{"unknown"};
        uint32_t timescale{0};
        std::int64_t durationMs{-1};
        int width{0};
        int height{0};
        int sampleRate{0};
        int channelCount{0};
        int nalLengthSize{0};
        SwByteArray keyFramePrefix{};
        std::uint64_t sampleCount{0};
    };

    struct Sample {
        std::int64_t ptsMs{-1};
        std::int64_t dtsMs{-1};
        bool keyFrame{false};
        SwByteArray payload{};
    };

    SwMp4Demuxer() = default;

    bool openFile(const std::string& path) {
        reset_();
        m_file.reset(new std::ifstream(path.c_str(), std::ios::binary));
        if (!m_file->is_open()) {
            return fail_("cannot open file: " + path);
        }
        m_file->seekg(0, std::ios::end);
        const std::streamoff size = m_file->tellg();
        if (size <= 0) {
            return fail_("empty file: " + path);
        }
        m_fileSize = static_cast<std::uint64_t>(size);
        return parse_();
    }

    bool openMemory(const SwByteArray& data) {
        reset_();
        if (data.isEmpty()) {
            return fail_("empty buffer");
        }
        m_memory = data;
        m_fileSize = static_cast<std::uint64_t>(m_memory.size());
        return parse_();
    }

    bool isOpen() const { return m_open; }
    const std::string& errorText() const { return m_error; }
    std::int64_t durationMs() const { return m_movieDurationMs; }

    std::vector<Track> tracks() const {
        std::vector<Track> result;
        result.reserve(m_tracks.size());
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            result.push_back(m_tracks[i].info);
        }
        return result;
    }

    const Track* trackById(uint32_t trackId) const {
        const TrackTables* tables = tablesById_(trackId);
        return tables ? &tables->info : nullptr;
    }

    /**
     * @brief Returns the first video track with a supported compressed codec, or nullptr.
     */
    const Track* videoTrack() const {
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            const Track& info = m_tracks[i].info;
            if (info.kind == TrackKind::Video &&
                info.videoCodec != SwVideoPacket::Codec::Unknown) {
                return &info;
            }
        }
        return nullptr;
    }

    std::uint64_t sampleCount(uint32_t trackId) const {
        const TrackTables* tables = tablesById_(trackId);
        return tables ? tables->samples.size() : 0;
    }

    bool readSample(uint32_t trackId, std::uint64_t index, Sample& out) {
        const TrackTables* tables = tablesById_(trackId);
        if (!tables || index >= tables->samples.size()) {
            return false;
        }
        const SampleRef& ref = tables->samples[static_cast<std::size_t>(index)];
        SwByteArray raw(static_cast<std::size_t>(ref.size), '\0');
        if (ref.size > 0 && !readAt_(ref.offset, raw.data(), ref.size)) {
            return false;
        }

        out.dtsMs = ticksToMs_(ref.dts, tables->info.timescale);
        out.ptsMs = samplePtsMs_(ref, tables->info.timescale);
        out.keyFrame = ref.keyFrame;
        if (tables->info.nalLengthSize > 0) {
            if (!convertToAnnexB_(std::move(raw), tables->info.nalLengthSize, out.payload)) {
                return false;
            }
        } else {
            out.payload = std::move(raw);
        }
        return true;
    }

    /**
     * @brief Cheap header sniff: returns whether the file starts with a plausible ISO BMFF box.
     *
     * Lets callers distinguish real MP4 containers from raw elementary streams that merely
     * carry an .mp4 extension (an Annex-B stream starts with 00 00 00 01 / 00 00 01, which
     * is never a valid box header).
     */
    static bool looksLikeBmff(const std::string& path) {
        std::ifstream input(path.c_str(), std::ios::binary);
        if (!input.is_open()) {
            return false;
        }
        std::uint8_t header[8];
        input.read(reinterpret_cast<char*>(header), 8);
        if (input.gcount() != 8) {
            return false;
        }
        const std::uint32_t size = (static_cast<std::uint32_t>(header[0]) << 24) |
                                   (static_cast<std::uint32_t>(header[1]) << 16) |
                                   (static_cast<std::uint32_t>(header[2]) << 8) |
                                   static_cast<std::uint32_t>(header[3]);
        if (size != 0 && size != 1 && size < 8) {
            return false;
        }
        const std::uint32_t type = (static_cast<std::uint32_t>(header[4]) << 24) |
                                   (static_cast<std::uint32_t>(header[5]) << 16) |
                                   (static_cast<std::uint32_t>(header[6]) << 8) |
                                   static_cast<std::uint32_t>(header[7]);
        static const char* kTopLevelTypes[] = {
            "ftyp", "styp", "moov", "moof", "mdat", "free", "skip", "wide", "pdin",
            "sidx", "uuid"
        };
        for (std::size_t i = 0; i < sizeof(kTopLevelTypes) / sizeof(kTopLevelTypes[0]); ++i) {
            if (type == fourcc_(kTopLevelTypes[i])) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Returns the index of the latest sync sample whose pts is <= targetMs (0 if none).
     *
     * O(log k) over the precomputed sync-sample index (k = key-frame count).
     */
    std::uint64_t findSyncSampleAtOrBefore(uint32_t trackId, std::int64_t targetMs) const {
        const TrackTables* tables = tablesById_(trackId);
        if (!tables || tables->syncByPtsMs.empty()) {
            return 0;
        }
        const std::vector<std::pair<std::int64_t, std::uint64_t>>& sync = tables->syncByPtsMs;
        const std::vector<std::pair<std::int64_t, std::uint64_t>>::const_iterator it =
            std::upper_bound(sync.begin(),
                             sync.end(),
                             targetMs,
                             [](std::int64_t target,
                                const std::pair<std::int64_t, std::uint64_t>& entry) {
                                 return target < entry.first;
                             });
        if (it == sync.begin()) {
            return 0;
        }
        return (it - 1)->second;
    }

private:
    struct SampleRef {
        std::uint64_t offset{0};
        std::uint32_t size{0};
        std::uint64_t dts{0};
        std::int32_t ctsOffset{0};
        bool keyFrame{true};
    };

    struct TrackTables {
        Track info{};
        std::vector<SampleRef> samples{};
        // Sync samples ordered by presentation time, for O(log n) seeks. Built once at
        // parse time; the rare key frame whose pts regresses is skipped (seeking then
        // resolves to the previous key, which is still a correct decode start).
        std::vector<std::pair<std::int64_t, std::uint64_t>> syncByPtsMs{};
    };

    /**
     * @brief Bounds-checked big-endian reader over an in-memory byte span.
     */
    struct ByteReader {
        const std::uint8_t* data{nullptr};
        std::size_t size{0};
        std::size_t pos{0};
        bool ok{true};

        ByteReader(const std::uint8_t* d, std::size_t n) : data(d), size(n) {}

        bool has(std::size_t n) const { return ok && pos + n <= size; }

        std::uint8_t u8() {
            if (!has(1)) { ok = false; return 0; }
            return data[pos++];
        }
        std::uint16_t u16() {
            if (!has(2)) { ok = false; return 0; }
            const std::uint16_t v = (static_cast<std::uint16_t>(data[pos]) << 8) | data[pos + 1];
            pos += 2;
            return v;
        }
        std::uint32_t u32() {
            if (!has(4)) { ok = false; return 0; }
            const std::uint32_t v = (static_cast<std::uint32_t>(data[pos]) << 24) |
                                    (static_cast<std::uint32_t>(data[pos + 1]) << 16) |
                                    (static_cast<std::uint32_t>(data[pos + 2]) << 8) |
                                    static_cast<std::uint32_t>(data[pos + 3]);
            pos += 4;
            return v;
        }
        std::uint64_t u64() {
            const std::uint64_t hi = u32();
            const std::uint64_t lo = u32();
            return (hi << 32) | lo;
        }
        void skip(std::size_t n) {
            if (!has(n)) { ok = false; return; }
            pos += n;
        }
        const std::uint8_t* bytes(std::size_t n) {
            if (!has(n)) { ok = false; return nullptr; }
            const std::uint8_t* p = data + pos;
            pos += n;
            return p;
        }
    };

    struct BoxSpan {
        std::uint32_t type{0};
        const std::uint8_t* payload{nullptr};
        std::size_t payloadSize{0};
    };

    static std::uint32_t fourcc_(const char* s) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 8) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(s[3]));
    }

    static std::string fourccName_(std::uint32_t type) {
        std::string name(4, '?');
        for (int i = 0; i < 4; ++i) {
            const char c = static_cast<char>((type >> (24 - 8 * i)) & 0xFF);
            name[static_cast<std::size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
        }
        return name;
    }

    static std::int64_t ticksToMs_(std::uint64_t ticks, std::uint32_t timescale) {
        if (timescale == 0) {
            return -1;
        }
        return static_cast<std::int64_t>((ticks * 1000ULL) / timescale);
    }

    // Composition offsets (ctts v1) can be negative; clamp the signed sum so a
    // pts before dts 0 never wraps through the unsigned overload.
    static std::int64_t samplePtsMs_(const SampleRef& ref, std::uint32_t timescale) {
        std::int64_t pts = static_cast<std::int64_t>(ref.dts) + ref.ctsOffset;
        if (pts < 0) {
            pts = 0;
        }
        return ticksToMs_(static_cast<std::uint64_t>(pts), timescale);
    }

    void reset_() {
        m_file.reset();
        m_memory = SwByteArray();
        m_fileSize = 0;
        m_open = false;
        m_error.clear();
        m_movieTimescale = 0;
        m_movieDurationMs = -1;
        m_tracks.clear();
    }

    bool fail_(const std::string& message) {
        m_error = message;
        swCWarning(kSwLogCategory_SwMp4Demuxer) << "[SwMp4Demuxer] " << message;
        return false;
    }

    bool readAt_(std::uint64_t offset, char* dst, std::size_t length) {
        if (length == 0) {
            return true;
        }
        if (offset > m_fileSize || m_fileSize - offset < length) {
            return false;
        }
        if (m_file) {
            m_file->clear();
            m_file->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            m_file->read(dst, static_cast<std::streamsize>(length));
            return m_file->gcount() == static_cast<std::streamsize>(length);
        }
        std::memcpy(dst, m_memory.constData() + offset, length);
        return true;
    }

    const TrackTables* tablesById_(uint32_t trackId) const {
        for (std::size_t i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].info.trackId == trackId) {
                return &m_tracks[i];
            }
        }
        return nullptr;
    }

    // ---- top-level parsing -----------------------------------------------------------------

    bool parse_() {
        static const std::size_t kMaxMoovSize = 64U * 1024U * 1024U;

        std::uint64_t offset = 0;
        std::uint64_t moovOffset = 0;
        std::uint64_t moovSize = 0;
        bool sawFtyp = false;

        while (offset + 8 <= m_fileSize) {
            std::uint8_t header[16];
            if (!readAt_(offset, reinterpret_cast<char*>(header), 8)) {
                return fail_("truncated box header");
            }
            std::uint64_t boxSize = (static_cast<std::uint32_t>(header[0]) << 24) |
                                    (static_cast<std::uint32_t>(header[1]) << 16) |
                                    (static_cast<std::uint32_t>(header[2]) << 8) |
                                    static_cast<std::uint32_t>(header[3]);
            const std::uint32_t boxType = (static_cast<std::uint32_t>(header[4]) << 24) |
                                          (static_cast<std::uint32_t>(header[5]) << 16) |
                                          (static_cast<std::uint32_t>(header[6]) << 8) |
                                          static_cast<std::uint32_t>(header[7]);
            std::uint64_t headerSize = 8;
            if (boxSize == 1) {
                if (!readAt_(offset + 8, reinterpret_cast<char*>(header + 8), 8)) {
                    return fail_("truncated large box header");
                }
                boxSize = 0;
                for (int i = 0; i < 8; ++i) {
                    boxSize = (boxSize << 8) | header[8 + i];
                }
                headerSize = 16;
            } else if (boxSize == 0) {
                boxSize = m_fileSize - offset;
            }
            if (boxSize < headerSize || boxSize > m_fileSize - offset) {
                return fail_("invalid box size for '" + fourccName_(boxType) + "'");
            }

            if (boxType == fourcc_("ftyp")) {
                sawFtyp = true;
            } else if (boxType == fourcc_("moof")) {
                return fail_("fragmented MP4 (moof) is not supported");
            } else if (boxType == fourcc_("moov")) {
                moovOffset = offset + headerSize;
                moovSize = boxSize - headerSize;
            }
            offset += boxSize;
        }

        if (moovSize == 0) {
            return fail_(sawFtyp ? "no moov box found" : "not an ISO BMFF file");
        }
        if (moovSize > kMaxMoovSize) {
            return fail_("moov box too large");
        }

        std::vector<std::uint8_t> moov(static_cast<std::size_t>(moovSize));
        if (!readAt_(moovOffset, reinterpret_cast<char*>(moov.data()), moov.size())) {
            return fail_("failed to read moov box");
        }
        if (!parseMoov_(moov.data(), moov.size())) {
            return false;
        }
        if (m_tracks.empty()) {
            return fail_("no playable track found");
        }
        m_open = true;
        return true;
    }

    static bool nextBox_(ByteReader& reader, BoxSpan& box) {
        if (!reader.has(8)) {
            return false;
        }
        std::uint64_t size = reader.u32();
        box.type = reader.u32();
        std::size_t headerSize = 8;
        if (size == 1) {
            size = reader.u64();
            headerSize = 16;
        } else if (size == 0) {
            size = (reader.size - reader.pos) + headerSize;
        }
        if (!reader.ok || size < headerSize ||
            size - headerSize > reader.size - reader.pos) {
            return false;
        }
        box.payloadSize = static_cast<std::size_t>(size - headerSize);
        box.payload = reader.data + reader.pos;
        reader.pos += box.payloadSize;
        return true;
    }

    static const std::uint8_t* findChildBox_(const std::uint8_t* payload,
                                             std::size_t payloadSize,
                                             std::uint32_t wantedType,
                                             std::size_t& childSize) {
        ByteReader reader(payload, payloadSize);
        BoxSpan box;
        while (nextBox_(reader, box)) {
            if (box.type == wantedType) {
                childSize = box.payloadSize;
                return box.payload;
            }
        }
        childSize = 0;
        return nullptr;
    }

    bool parseMoov_(const std::uint8_t* payload, std::size_t payloadSize) {
        ByteReader reader(payload, payloadSize);
        BoxSpan box;
        while (nextBox_(reader, box)) {
            if (box.type == fourcc_("mvhd")) {
                parseMvhd_(box.payload, box.payloadSize);
            } else if (box.type == fourcc_("trak")) {
                TrackTables tables;
                if (parseTrak_(box.payload, box.payloadSize, tables)) {
                    m_tracks.push_back(tables);
                }
            }
        }
        return true;
    }

    void parseMvhd_(const std::uint8_t* payload, std::size_t payloadSize) {
        ByteReader reader(payload, payloadSize);
        const std::uint8_t version = reader.u8();
        reader.skip(3);
        std::uint64_t duration = 0;
        if (version == 1) {
            reader.skip(16); // creation + modification (64-bit)
            m_movieTimescale = reader.u32();
            duration = reader.u64();
        } else {
            reader.skip(8); // creation + modification (32-bit)
            m_movieTimescale = reader.u32();
            duration = reader.u32();
        }
        if (reader.ok && m_movieTimescale > 0 &&
            duration != 0xFFFFFFFFULL && duration != 0xFFFFFFFFFFFFFFFFULL) {
            m_movieDurationMs = ticksToMs_(duration, m_movieTimescale);
        }
    }

    bool parseTrak_(const std::uint8_t* payload, std::size_t payloadSize, TrackTables& tables) {
        std::size_t tkhdSize = 0;
        const std::uint8_t* tkhd = findChildBox_(payload, payloadSize, fourcc_("tkhd"), tkhdSize);
        if (tkhd) {
            parseTkhd_(tkhd, tkhdSize, tables.info);
        }

        std::size_t mdiaSize = 0;
        const std::uint8_t* mdia = findChildBox_(payload, payloadSize, fourcc_("mdia"), mdiaSize);
        if (!mdia) {
            return false;
        }

        std::size_t mdhdSize = 0;
        const std::uint8_t* mdhd = findChildBox_(mdia, mdiaSize, fourcc_("mdhd"), mdhdSize);
        if (mdhd) {
            parseMdhd_(mdhd, mdhdSize, tables.info);
        }
        if (tables.info.timescale == 0) {
            return false;
        }

        std::size_t hdlrSize = 0;
        const std::uint8_t* hdlr = findChildBox_(mdia, mdiaSize, fourcc_("hdlr"), hdlrSize);
        if (hdlr) {
            ByteReader reader(hdlr, hdlrSize);
            reader.skip(8); // version/flags + pre_defined
            const std::uint32_t handler = reader.u32();
            if (handler == fourcc_("vide")) {
                tables.info.kind = TrackKind::Video;
            } else if (handler == fourcc_("soun")) {
                tables.info.kind = TrackKind::Audio;
            }
        }
        if (tables.info.kind == TrackKind::Unknown) {
            return false;
        }

        std::size_t minfSize = 0;
        const std::uint8_t* minf = findChildBox_(mdia, mdiaSize, fourcc_("minf"), minfSize);
        if (!minf) {
            return false;
        }
        std::size_t stblSize = 0;
        const std::uint8_t* stbl = findChildBox_(minf, minfSize, fourcc_("stbl"), stblSize);
        if (!stbl) {
            return false;
        }
        return parseStbl_(stbl, stblSize, tables);
    }

    void parseTkhd_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        ByteReader reader(payload, payloadSize);
        const std::uint8_t version = reader.u8();
        reader.skip(3);
        if (version == 1) {
            reader.skip(16);
            info.trackId = reader.u32();
        } else {
            reader.skip(8);
            info.trackId = reader.u32();
        }
        // reserved(4) + duration(4/8) + reserved(8) + layer(2) + group(2) + volume(2) +
        // reserved(2) + matrix(36)
        reader.skip(version == 1 ? (4U + 8U) : (4U + 4U));
        reader.skip(8 + 2 + 2 + 2 + 2 + 36);
        const std::uint32_t width = reader.u32();
        const std::uint32_t height = reader.u32();
        if (reader.ok) {
            info.width = static_cast<int>(width >> 16);
            info.height = static_cast<int>(height >> 16);
        }
    }

    void parseMdhd_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        ByteReader reader(payload, payloadSize);
        const std::uint8_t version = reader.u8();
        reader.skip(3);
        std::uint64_t duration = 0;
        if (version == 1) {
            reader.skip(16);
            info.timescale = reader.u32();
            duration = reader.u64();
        } else {
            reader.skip(8);
            info.timescale = reader.u32();
            duration = reader.u32();
        }
        if (reader.ok && info.timescale > 0 &&
            duration != 0xFFFFFFFFULL && duration != 0xFFFFFFFFFFFFFFFFULL) {
            info.durationMs = ticksToMs_(duration, info.timescale);
        }
    }

    // ---- sample tables ---------------------------------------------------------------------

    bool parseStbl_(const std::uint8_t* payload, std::size_t payloadSize, TrackTables& tables) {
        const std::uint8_t* stsd = nullptr;
        std::size_t stsdSize = 0;
        const std::uint8_t* stts = nullptr;
        std::size_t sttsSize = 0;
        const std::uint8_t* ctts = nullptr;
        std::size_t cttsSize = 0;
        const std::uint8_t* stsc = nullptr;
        std::size_t stscSize = 0;
        const std::uint8_t* stsz = nullptr;
        std::size_t stszSize = 0;
        const std::uint8_t* stco = nullptr;
        std::size_t stcoSize = 0;
        bool chunkOffsets64 = false;
        const std::uint8_t* stss = nullptr;
        std::size_t stssSize = 0;

        ByteReader reader(payload, payloadSize);
        BoxSpan box;
        while (nextBox_(reader, box)) {
            if (box.type == fourcc_("stsd")) {
                stsd = box.payload;
                stsdSize = box.payloadSize;
            } else if (box.type == fourcc_("stts")) {
                stts = box.payload;
                sttsSize = box.payloadSize;
            } else if (box.type == fourcc_("ctts")) {
                ctts = box.payload;
                cttsSize = box.payloadSize;
            } else if (box.type == fourcc_("stsc")) {
                stsc = box.payload;
                stscSize = box.payloadSize;
            } else if (box.type == fourcc_("stsz")) {
                stsz = box.payload;
                stszSize = box.payloadSize;
            } else if (box.type == fourcc_("stco")) {
                stco = box.payload;
                stcoSize = box.payloadSize;
                chunkOffsets64 = false;
            } else if (box.type == fourcc_("co64")) {
                stco = box.payload;
                stcoSize = box.payloadSize;
                chunkOffsets64 = true;
            } else if (box.type == fourcc_("stss")) {
                stss = box.payload;
                stssSize = box.payloadSize;
            }
        }
        if (!stsd || !stts || !stsc || !stsz || !stco) {
            return false;
        }
        if (!parseStsd_(stsd, stsdSize, tables.info)) {
            return false;
        }

        // stsz: sizes.
        std::vector<std::uint32_t> sizes;
        {
            ByteReader r(stsz, stszSize);
            r.skip(4);
            const std::uint32_t constantSize = r.u32();
            const std::uint32_t count = r.u32();
            if (!r.ok || count > kMaxSamples_) {
                return false;
            }
            sizes.resize(count);
            if (constantSize != 0) {
                for (std::uint32_t i = 0; i < count; ++i) {
                    sizes[i] = constantSize;
                }
            } else {
                for (std::uint32_t i = 0; i < count; ++i) {
                    sizes[i] = r.u32();
                }
                if (!r.ok) {
                    return false;
                }
            }
        }
        const std::size_t sampleTotal = sizes.size();
        if (sampleTotal == 0) {
            return false;
        }
        tables.samples.resize(sampleTotal);
        for (std::size_t i = 0; i < sampleTotal; ++i) {
            tables.samples[i].size = sizes[i];
        }

        // stts: decode timestamps.
        {
            ByteReader r(stts, sttsSize);
            r.skip(4);
            const std::uint32_t entryCount = r.u32();
            std::uint64_t dts = 0;
            std::size_t sampleIndex = 0;
            for (std::uint32_t e = 0; e < entryCount && r.ok; ++e) {
                const std::uint32_t count = r.u32();
                const std::uint32_t delta = r.u32();
                for (std::uint32_t i = 0; i < count && sampleIndex < sampleTotal; ++i) {
                    tables.samples[sampleIndex].dts = dts;
                    dts += delta;
                    ++sampleIndex;
                }
            }
            if (!r.ok || sampleIndex != sampleTotal) {
                return false;
            }
        }

        // ctts: composition offsets (optional).
        if (ctts) {
            ByteReader r(ctts, cttsSize);
            const std::uint8_t version = r.u8();
            r.skip(3);
            const std::uint32_t entryCount = r.u32();
            std::size_t sampleIndex = 0;
            for (std::uint32_t e = 0; e < entryCount && r.ok; ++e) {
                const std::uint32_t count = r.u32();
                const std::uint32_t rawOffset = r.u32();
                const std::int32_t offset =
                    (version == 0) ? static_cast<std::int32_t>(
                                         rawOffset > 0x7FFFFFFFU ? 0x7FFFFFFFU : rawOffset)
                                   : static_cast<std::int32_t>(rawOffset);
                for (std::uint32_t i = 0; i < count && sampleIndex < sampleTotal; ++i) {
                    tables.samples[sampleIndex].ctsOffset = offset;
                    ++sampleIndex;
                }
            }
            if (!r.ok) {
                return false;
            }
        }

        // stsc + stco/co64: chunk map -> per-sample file offsets.
        {
            struct StscEntry {
                std::uint32_t firstChunk{0};
                std::uint32_t samplesPerChunk{0};
            };
            std::vector<StscEntry> stscEntries;
            {
                ByteReader r(stsc, stscSize);
                r.skip(4);
                const std::uint32_t entryCount = r.u32();
                if (!r.ok || entryCount > kMaxSamples_) {
                    return false;
                }
                stscEntries.resize(entryCount);
                for (std::uint32_t e = 0; e < entryCount; ++e) {
                    stscEntries[e].firstChunk = r.u32();
                    stscEntries[e].samplesPerChunk = r.u32();
                    r.skip(4); // sample_description_index
                }
                if (!r.ok || stscEntries.empty()) {
                    return false;
                }
            }

            ByteReader r(stco, stcoSize);
            r.skip(4);
            const std::uint32_t chunkCount = r.u32();
            if (!r.ok || chunkCount > kMaxSamples_) {
                return false;
            }
            std::size_t sampleIndex = 0;
            std::size_t stscIndex = 0;
            for (std::uint32_t chunk = 1; chunk <= chunkCount && sampleIndex < sampleTotal;
                 ++chunk) {
                while (stscIndex + 1 < stscEntries.size() &&
                       stscEntries[stscIndex + 1].firstChunk <= chunk) {
                    ++stscIndex;
                }
                std::uint64_t offset = chunkOffsets64 ? r.u64() : r.u32();
                if (!r.ok) {
                    return false;
                }
                const std::uint32_t samplesInChunk = stscEntries[stscIndex].samplesPerChunk;
                for (std::uint32_t i = 0; i < samplesInChunk && sampleIndex < sampleTotal; ++i) {
                    tables.samples[sampleIndex].offset = offset;
                    offset += tables.samples[sampleIndex].size;
                    ++sampleIndex;
                }
            }
            if (sampleIndex != sampleTotal) {
                return false;
            }
        }

        // stss: sync samples (absent => every sample is a key frame).
        if (stss) {
            for (std::size_t i = 0; i < sampleTotal; ++i) {
                tables.samples[i].keyFrame = false;
            }
            ByteReader r(stss, stssSize);
            r.skip(4);
            const std::uint32_t entryCount = r.u32();
            for (std::uint32_t e = 0; e < entryCount && r.ok; ++e) {
                const std::uint32_t sampleNumber = r.u32(); // 1-based
                if (sampleNumber >= 1 && sampleNumber <= sampleTotal) {
                    tables.samples[sampleNumber - 1].keyFrame = true;
                }
            }
            if (!r.ok) {
                return false;
            }
        }

        tables.info.sampleCount = sampleTotal;
        if (tables.info.durationMs < 0 && !tables.samples.empty()) {
            const SampleRef& last = tables.samples.back();
            tables.info.durationMs = ticksToMs_(last.dts, tables.info.timescale);
        }

        // Precompute the sync-sample index used by findSyncSampleAtOrBefore.
        tables.syncByPtsMs.clear();
        std::int64_t lastSyncPtsMs = 0;
        bool haveSync = false;
        for (std::size_t i = 0; i < sampleTotal; ++i) {
            if (!tables.samples[i].keyFrame) {
                continue;
            }
            const std::int64_t ptsMs = samplePtsMs_(tables.samples[i], tables.info.timescale);
            if (haveSync && ptsMs < lastSyncPtsMs) {
                continue;
            }
            tables.syncByPtsMs.push_back(
                std::pair<std::int64_t, std::uint64_t>(ptsMs, static_cast<std::uint64_t>(i)));
            lastSyncPtsMs = ptsMs;
            haveSync = true;
        }
        return true;
    }

    // ---- sample descriptions ---------------------------------------------------------------

    bool parseStsd_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        ByteReader reader(payload, payloadSize);
        reader.skip(4); // version/flags
        const std::uint32_t entryCount = reader.u32();
        if (!reader.ok || entryCount == 0) {
            return false;
        }
        BoxSpan entry;
        if (!nextBox_(reader, entry)) {
            return false;
        }

        if (info.kind == TrackKind::Video) {
            return parseVisualSampleEntry_(entry, info);
        }
        return parseAudioSampleEntry_(entry, info);
    }

    bool parseVisualSampleEntry_(const BoxSpan& entry, Track& info) {
        // VisualSampleEntry: 6 reserved + 2 data_reference_index + 16 pre_defined/reserved +
        // 2 width + 2 height + 4+4 resolution + 4 reserved + 2 frame_count +
        // 32 compressorname + 2 depth + 2 pre_defined = 78 bytes, then child boxes.
        static const std::size_t kVisualFixedSize = 78;
        if (entry.payloadSize < kVisualFixedSize) {
            return false;
        }
        ByteReader reader(entry.payload, entry.payloadSize);
        reader.skip(6 + 2 + 16);
        const int width = static_cast<int>(reader.u16());
        const int height = static_cast<int>(reader.u16());
        if (reader.ok) {
            if (info.width <= 0) {
                info.width = width;
            }
            if (info.height <= 0) {
                info.height = height;
            }
        }

        const std::uint8_t* children = entry.payload + kVisualFixedSize;
        const std::size_t childrenSize = entry.payloadSize - kVisualFixedSize;

        if (entry.type == fourcc_("avc1") || entry.type == fourcc_("avc3")) {
            std::size_t configSize = 0;
            const std::uint8_t* config =
                findChildBox_(children, childrenSize, fourcc_("avcC"), configSize);
            if (!config || !parseAvcC_(config, configSize, info)) {
                return false;
            }
            info.videoCodec = SwVideoPacket::Codec::H264;
            info.codecName = "h264";
            return true;
        }
        if (entry.type == fourcc_("hvc1") || entry.type == fourcc_("hev1")) {
            std::size_t configSize = 0;
            const std::uint8_t* config =
                findChildBox_(children, childrenSize, fourcc_("hvcC"), configSize);
            if (!config || !parseHvcC_(config, configSize, info)) {
                return false;
            }
            info.videoCodec = SwVideoPacket::Codec::H265;
            info.codecName = "h265";
            return true;
        }
        if (entry.type == fourcc_("av01")) {
            std::size_t configSize = 0;
            const std::uint8_t* config =
                findChildBox_(children, childrenSize, fourcc_("av1C"), configSize);
            if (!config || !parseAv1C_(config, configSize, info)) {
                return false;
            }
            info.videoCodec = SwVideoPacket::Codec::AV1;
            info.codecName = "av1";
            return true;
        }
        swCWarning(kSwLogCategory_SwMp4Demuxer)
            << "[SwMp4Demuxer] unsupported video sample entry '" << fourccName_(entry.type)
            << "'";
        info.codecName = fourccName_(entry.type);
        return true; // keep the track visible with Codec::Unknown
    }

    bool parseAudioSampleEntry_(const BoxSpan& entry, Track& info) {
        // AudioSampleEntry: 6 reserved + 2 data_reference_index + 8 reserved +
        // 2 channelcount + 2 samplesize + 2 pre_defined + 2 reserved + 4 samplerate(16.16)
        static const std::size_t kAudioFixedSize = 28;
        if (entry.payloadSize < kAudioFixedSize) {
            return false;
        }
        ByteReader reader(entry.payload, entry.payloadSize);
        reader.skip(6 + 2 + 8);
        info.channelCount = static_cast<int>(reader.u16());
        reader.skip(2 + 2 + 2);
        info.sampleRate = static_cast<int>(reader.u32() >> 16);
        if (!reader.ok) {
            return false;
        }

        const std::uint8_t* children = entry.payload + kAudioFixedSize;
        const std::size_t childrenSize = entry.payloadSize - kAudioFixedSize;

        if (entry.type == fourcc_("mp4a")) {
            std::size_t esdsSize = 0;
            if (findChildBox_(children, childrenSize, fourcc_("esds"), esdsSize)) {
                info.audioCodec = SwAudioPacket::Codec::AAC;
                info.codecName = "aac";
            } else {
                info.codecName = "mp4a";
            }
            return true;
        }
        if (entry.type == fourcc_("Opus")) {
            info.audioCodec = SwAudioPacket::Codec::Opus;
            info.codecName = "opus";
            return true;
        }
        info.codecName = fourccName_(entry.type);
        return true;
    }

    bool parseAvcC_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        ByteReader reader(payload, payloadSize);
        reader.skip(4); // configurationVersion + profile + compat + level
        const std::uint8_t lengthByte = reader.u8();
        const std::uint8_t spsCount = reader.u8() & 0x1F;
        if (!reader.ok) {
            return false;
        }
        info.nalLengthSize = static_cast<int>(lengthByte & 0x3) + 1;

        std::vector<char> prefix;
        for (std::uint8_t i = 0; i < spsCount; ++i) {
            if (!appendPrefixedNal_(reader, prefix)) {
                return false;
            }
        }
        const std::uint8_t ppsCount = reader.u8();
        for (std::uint8_t i = 0; reader.ok && i < ppsCount; ++i) {
            if (!appendPrefixedNal_(reader, prefix)) {
                return false;
            }
        }
        if (!reader.ok || prefix.empty()) {
            return false;
        }
        info.keyFramePrefix = SwByteArray(prefix.data(), prefix.size());
        return true;
    }

    bool parseHvcC_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        // HEVCDecoderConfigurationRecord: 22 fixed bytes, then NAL unit arrays.
        if (payloadSize < 23) {
            return false;
        }
        ByteReader reader(payload, payloadSize);
        reader.skip(21);
        const std::uint8_t lengthByte = reader.u8();
        const std::uint8_t arrayCount = reader.u8();
        if (!reader.ok) {
            return false;
        }
        info.nalLengthSize = static_cast<int>(lengthByte & 0x3) + 1;

        std::vector<char> prefix;
        for (std::uint8_t a = 0; reader.ok && a < arrayCount; ++a) {
            reader.skip(1); // completeness + NAL unit type
            const std::uint16_t naluCount = reader.u16();
            for (std::uint16_t i = 0; reader.ok && i < naluCount; ++i) {
                if (!appendPrefixedNal_(reader, prefix)) {
                    return false;
                }
            }
        }
        if (!reader.ok || prefix.empty()) {
            return false;
        }
        info.keyFramePrefix = SwByteArray(prefix.data(), prefix.size());
        return true;
    }

    bool parseAv1C_(const std::uint8_t* payload, std::size_t payloadSize, Track& info) {
        // AV1CodecConfigurationRecord: 4 fixed bytes, then optional configOBUs.
        if (payloadSize < 4 || (payload[0] & 0x80) == 0) {
            return false;
        }
        info.nalLengthSize = 0; // AV1 samples are raw OBU streams
        if (payloadSize > 4) {
            info.keyFramePrefix = SwByteArray(
                reinterpret_cast<const char*>(payload + 4), payloadSize - 4);
        }
        return true;
    }

    static bool appendPrefixedNal_(ByteReader& reader, std::vector<char>& prefix) {
        const std::uint16_t length = reader.u16();
        const std::uint8_t* data = reader.bytes(length);
        if (!reader.ok || !data) {
            return false;
        }
        static const char kStartCode[4] = {0, 0, 0, 1};
        prefix.insert(prefix.end(), kStartCode, kStartCode + 4);
        prefix.insert(prefix.end(),
                      reinterpret_cast<const char*>(data),
                      reinterpret_cast<const char*>(data) + length);
        return true;
    }

    static bool convertToAnnexB_(SwByteArray raw, int nalLengthSize, SwByteArray& out) {
        const std::size_t total = static_cast<std::size_t>(raw.size());
        if (total == 0) {
            out = SwByteArray();
            return true;
        }

        // NAL walking + validation shared with SwRtpPacketizer (media/SwHevcBitstream.h).
        if (nalLengthSize == 4) {
            // Dominant layout: the 4-byte length prefix is rewritten in place as a
            // 4-byte start code — no extra allocation or copy on the per-frame path.
            std::uint8_t* data = reinterpret_cast<std::uint8_t*>(raw.data());
            const bool parsed = swForEachLengthPrefixedNalUnit(
                data, total, 4, [data](const SwLengthPrefixedNalUnitView& nal) {
                    std::uint8_t* prefix = data + nal.offset - 4;
                    prefix[0] = 0;
                    prefix[1] = 0;
                    prefix[2] = 0;
                    prefix[3] = 1;
                });
            if (!parsed) {
                return false;
            }
            out = std::move(raw);
            return true;
        }

        const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(raw.constData());
        std::vector<char> converted;
        converted.reserve(total + 16);
        const bool parsed = swForEachLengthPrefixedNalUnit(
            src, total, static_cast<std::size_t>(nalLengthSize),
            [&converted, src](const SwLengthPrefixedNalUnitView& nal) {
                static const char kStartCode[4] = {0, 0, 0, 1};
                converted.insert(converted.end(), kStartCode, kStartCode + 4);
                converted.insert(converted.end(),
                                 reinterpret_cast<const char*>(src + nal.offset),
                                 reinterpret_cast<const char*>(src + nal.offset) + nal.size);
            });
        if (!parsed) {
            return false;
        }
        out = SwByteArray(converted.data(), converted.size());
        return true;
    }

    static const std::uint32_t kMaxSamples_ = 16U * 1024U * 1024U;

    std::unique_ptr<std::ifstream> m_file{};
    SwByteArray m_memory{};
    std::uint64_t m_fileSize{0};
    bool m_open{false};
    std::string m_error{};
    std::uint32_t m_movieTimescale{0};
    std::int64_t m_movieDurationMs{-1};
    std::vector<TrackTables> m_tracks{};
};
