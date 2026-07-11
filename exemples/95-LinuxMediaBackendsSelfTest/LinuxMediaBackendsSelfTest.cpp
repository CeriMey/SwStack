/***************************************************************************************************
 * LinuxMediaBackendsSelfTest
 *
 * Validates the Linux media backends and the platform-neutral MP4 demuxer:
 *  - SwMp4Demuxer: box parsing, sample tables, Annex-B conversion, seek helpers, error paths
 *    (runs on every platform — the demuxer is dependency-free).
 *  - SwLinuxMovieSource: real-time paced playback of a crafted MP4 file, seek + discontinuity.
 *  - SwMediaSourceFactory: file routing towards the platform movie source.
 *  - SwLinuxVideoSource (V4L2), SwAlsaAudioSink (dlopen libasound) and SwOpusAudioDecoder
 *    (dlopen libopus): graceful behaviour whether or not the device/library is present.
 ***************************************************************************************************/

#include "media/SwAudioDecoder.h"
#include "media/SwAudioOutput.h"
#include "media/SwMediaSourceFactory.h"
#include "media/SwMediaTimelineSource.h"
#include "media/SwMp4Demuxer.h"
#include "media/SwMp4MovieSource.h"

#if defined(__linux__)
#include "media/SwAlsaAudioSink.h"
#include "media/SwLinuxVideoSource.h"
#include "media/SwOpusAudioDecoder.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[LinuxMediaBackendsSelfTest] FAIL " << message << "\n";
        ++g_failures;
        return false;
    }
    return true;
}

bool sameBytes(const SwByteArray& actual, const void* expected, std::size_t size) {
    return static_cast<std::size_t>(actual.size()) == size &&
           (size == 0 || std::memcmp(actual.constData(), expected, size) == 0);
}

// ---- minimal ISO BMFF writer ----------------------------------------------------------------

class Mp4Writer {
public:
    void u8(std::uint8_t v) { m_bytes.push_back(static_cast<char>(v)); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v >> 8));
        u8(static_cast<std::uint8_t>(v));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v >> 16));
        u16(static_cast<std::uint16_t>(v));
    }
    void bytes(const void* data, std::size_t size) {
        const char* p = static_cast<const char*>(data);
        m_bytes.insert(m_bytes.end(), p, p + size);
    }
    void zeros(std::size_t size) {
        m_bytes.insert(m_bytes.end(), size, '\0');
    }
    std::size_t boxBegin(const char* type) {
        const std::size_t sizePos = m_bytes.size();
        u32(0);
        bytes(type, 4);
        return sizePos;
    }
    void boxEnd(std::size_t sizePos) {
        const std::uint32_t size = static_cast<std::uint32_t>(m_bytes.size() - sizePos);
        m_bytes[sizePos + 0] = static_cast<char>((size >> 24) & 0xFF);
        m_bytes[sizePos + 1] = static_cast<char>((size >> 16) & 0xFF);
        m_bytes[sizePos + 2] = static_cast<char>((size >> 8) & 0xFF);
        m_bytes[sizePos + 3] = static_cast<char>(size & 0xFF);
    }
    std::size_t size() const { return m_bytes.size(); }
    SwByteArray toByteArray() const { return SwByteArray(m_bytes.data(), m_bytes.size()); }

private:
    std::vector<char> m_bytes;
};

const std::uint8_t kSps[] = {0x67, 0x42, 0x00, 0x1E};
const std::uint8_t kPps[] = {0x68, 0xCE};
const std::uint8_t kAv1ConfigObus[] = {0x0A, 0x03, 0x01, 0x02, 0x03};

// Sample i carries one fake NAL: header byte + (8 + i) pattern bytes.
std::vector<std::uint8_t> makeH264Nal(int index, bool keyFrame) {
    std::vector<std::uint8_t> nal;
    nal.push_back(keyFrame ? 0x65 : 0x41);
    for (int i = 0; i < 8 + index; ++i) {
        nal.push_back(static_cast<std::uint8_t>(0x10 * index + i));
    }
    return nal;
}

std::vector<std::vector<std::uint8_t> > makeH264Samples() {
    // 5 samples, key frames at index 0 and 3; sample 4 holds TWO NAL units.
    std::vector<std::vector<std::uint8_t> > samples;
    for (int i = 0; i < 5; ++i) {
        const bool key = (i == 0 || i == 3);
        std::vector<std::uint8_t> nal = makeH264Nal(i, key);
        std::vector<std::uint8_t> sample;
        sample.push_back(0);
        sample.push_back(0);
        sample.push_back(0);
        sample.push_back(static_cast<std::uint8_t>(nal.size()));
        sample.insert(sample.end(), nal.begin(), nal.end());
        if (i == 4) {
            std::vector<std::uint8_t> extra = makeH264Nal(9, false);
            sample.push_back(0);
            sample.push_back(0);
            sample.push_back(0);
            sample.push_back(static_cast<std::uint8_t>(extra.size()));
            sample.insert(sample.end(), extra.begin(), extra.end());
        }
        samples.push_back(sample);
    }
    return samples;
}

void writeVisualSampleEntryHeader(Mp4Writer& w, int width, int height) {
    w.zeros(6);
    w.u16(1); // data_reference_index
    w.zeros(16);
    w.u16(static_cast<std::uint16_t>(width));
    w.u16(static_cast<std::uint16_t>(height));
    w.u32(0x00480000); // horizresolution
    w.u32(0x00480000); // vertresolution
    w.u32(0);
    w.u16(1); // frame_count
    w.zeros(32); // compressorname
    w.u16(24); // depth
    w.u16(0xFFFF); // pre_defined
}

// Builds a progressive MP4 with one video track. codec: 0 = H264 (avc1/avcC), 1 = AV1 (av01).
// A negative ctsOffset emits a version-1 ctts box (signed composition offsets).
// syncSamples: 1-based stss entries; nullptr keeps the default {1, 4}.
SwByteArray buildTestMp4(const std::vector<std::vector<std::uint8_t> >& samples,
                         int codec,
                         std::uint32_t timescale,
                         std::uint32_t sampleDelta,
                         std::int32_t ctsOffset,
                         const std::vector<std::uint32_t>* syncSamples = nullptr) {
    Mp4Writer w;

    // ftyp
    {
        const std::size_t box = w.boxBegin("ftyp");
        w.bytes("isom", 4);
        w.u32(0);
        w.boxEnd(box);
    }

    // mdat (record absolute sample offsets while writing)
    std::vector<std::uint32_t> offsets;
    {
        const std::size_t box = w.boxBegin("mdat");
        for (std::size_t i = 0; i < samples.size(); ++i) {
            offsets.push_back(static_cast<std::uint32_t>(w.size()));
            w.bytes(samples[i].data(), samples[i].size());
        }
        w.boxEnd(box);
    }

    // moov
    const std::size_t moov = w.boxBegin("moov");
    {
        const std::size_t mvhd = w.boxBegin("mvhd");
        w.u32(0); // version/flags
        w.u32(0); // creation
        w.u32(0); // modification
        w.u32(1000); // movie timescale
        w.u32(200); // duration: 200 ms
        w.u32(0x00010000); // rate
        w.u16(0x0100); // volume
        w.zeros(2 + 8);
        w.u32(0x00010000); w.u32(0); w.u32(0);
        w.u32(0); w.u32(0x00010000); w.u32(0);
        w.u32(0); w.u32(0); w.u32(0x40000000);
        w.zeros(24); // pre_defined
        w.u32(2); // next track id
        w.boxEnd(mvhd);
    }
    const std::size_t trak = w.boxBegin("trak");
    {
        const std::size_t tkhd = w.boxBegin("tkhd");
        w.u32(0x00000007); // version 0 + flags (enabled)
        w.u32(0); // creation
        w.u32(0); // modification
        w.u32(1); // track id
        w.u32(0); // reserved
        w.u32(200); // duration in movie timescale
        w.zeros(8);
        w.u16(0); // layer
        w.u16(0); // alternate group
        w.u16(0); // volume
        w.u16(0); // reserved
        w.u32(0x00010000); w.u32(0); w.u32(0);
        w.u32(0); w.u32(0x00010000); w.u32(0);
        w.u32(0); w.u32(0); w.u32(0x40000000);
        w.u32(320U << 16); // width 16.16
        w.u32(240U << 16); // height 16.16
        w.boxEnd(tkhd);
    }
    const std::size_t mdia = w.boxBegin("mdia");
    {
        const std::size_t mdhd = w.boxBegin("mdhd");
        w.u32(0); // version/flags
        w.u32(0);
        w.u32(0);
        w.u32(timescale);
        w.u32(sampleDelta * static_cast<std::uint32_t>(samples.size())); // duration
        w.u16(0x55C4); // language "und"
        w.u16(0);
        w.boxEnd(mdhd);
    }
    {
        const std::size_t hdlr = w.boxBegin("hdlr");
        w.u32(0);
        w.u32(0);
        w.bytes("vide", 4);
        w.zeros(12);
        w.u8(0); // empty name
        w.boxEnd(hdlr);
    }
    const std::size_t minf = w.boxBegin("minf");
    const std::size_t stbl = w.boxBegin("stbl");
    {
        const std::size_t stsd = w.boxBegin("stsd");
        w.u32(0);
        w.u32(1);
        if (codec == 0) {
            const std::size_t avc1 = w.boxBegin("avc1");
            writeVisualSampleEntryHeader(w, 320, 240);
            const std::size_t avcC = w.boxBegin("avcC");
            w.u8(1); // configurationVersion
            w.u8(0x42); // profile
            w.u8(0x00); // compat
            w.u8(0x1E); // level
            w.u8(0xFF); // lengthSizeMinusOne = 3
            w.u8(0xE1); // 1 SPS
            w.u16(sizeof(kSps));
            w.bytes(kSps, sizeof(kSps));
            w.u8(1); // 1 PPS
            w.u16(sizeof(kPps));
            w.bytes(kPps, sizeof(kPps));
            w.boxEnd(avcC);
            w.boxEnd(avc1);
        } else {
            const std::size_t av01 = w.boxBegin("av01");
            writeVisualSampleEntryHeader(w, 320, 240);
            const std::size_t av1C = w.boxBegin("av1C");
            w.u8(0x81); // marker + version
            w.u8(0x00);
            w.u8(0x00);
            w.u8(0x00);
            w.bytes(kAv1ConfigObus, sizeof(kAv1ConfigObus));
            w.boxEnd(av1C);
            w.boxEnd(av01);
        }
        w.boxEnd(stsd);
    }
    {
        const std::size_t stts = w.boxBegin("stts");
        w.u32(0);
        w.u32(1);
        w.u32(static_cast<std::uint32_t>(samples.size()));
        w.u32(sampleDelta);
        w.boxEnd(stts);
    }
    if (ctsOffset != 0) {
        const std::size_t ctts = w.boxBegin("ctts");
        w.u32(ctsOffset < 0 ? 0x01000000U : 0U); // version 1 for signed offsets
        w.u32(1);
        w.u32(static_cast<std::uint32_t>(samples.size()));
        w.u32(static_cast<std::uint32_t>(ctsOffset));
        w.boxEnd(ctts);
    }
    {
        static const std::vector<std::uint32_t> kDefaultSync{1, 4};
        const std::vector<std::uint32_t>& sync = syncSamples ? *syncSamples : kDefaultSync;
        const std::size_t stss = w.boxBegin("stss");
        w.u32(0);
        w.u32(static_cast<std::uint32_t>(sync.size()));
        for (std::size_t i = 0; i < sync.size(); ++i) {
            w.u32(sync[i]);
        }
        w.boxEnd(stss);
    }
    {
        const std::size_t stsc = w.boxBegin("stsc");
        w.u32(0);
        w.u32(1);
        w.u32(1); // first_chunk
        w.u32(static_cast<std::uint32_t>(samples.size())); // samples_per_chunk
        w.u32(1); // sample_description_index
        w.boxEnd(stsc);
    }
    {
        const std::size_t stsz = w.boxBegin("stsz");
        w.u32(0);
        w.u32(0); // per-sample sizes
        w.u32(static_cast<std::uint32_t>(samples.size()));
        for (std::size_t i = 0; i < samples.size(); ++i) {
            w.u32(static_cast<std::uint32_t>(samples[i].size()));
        }
        w.boxEnd(stsz);
    }
    {
        const std::size_t stco = w.boxBegin("stco");
        w.u32(0);
        w.u32(1);
        w.u32(offsets.empty() ? 0 : offsets[0]);
        w.boxEnd(stco);
    }
    w.boxEnd(stbl);
    w.boxEnd(minf);
    w.boxEnd(mdia);
    w.boxEnd(trak);
    w.boxEnd(moov);

    return w.toByteArray();
}

SwByteArray annexBExpected(const std::vector<std::uint8_t>& lengthPrefixed) {
    // Converts the crafted 4-byte length-prefixed sample to its expected Annex-B form.
    std::vector<char> out;
    std::size_t pos = 0;
    while (pos + 4 <= lengthPrefixed.size()) {
        std::uint32_t size = (static_cast<std::uint32_t>(lengthPrefixed[pos]) << 24) |
                             (static_cast<std::uint32_t>(lengthPrefixed[pos + 1]) << 16) |
                             (static_cast<std::uint32_t>(lengthPrefixed[pos + 2]) << 8) |
                             static_cast<std::uint32_t>(lengthPrefixed[pos + 3]);
        pos += 4;
        out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(1);
        out.insert(out.end(),
                   lengthPrefixed.begin() + static_cast<std::ptrdiff_t>(pos),
                   lengthPrefixed.begin() + static_cast<std::ptrdiff_t>(pos + size));
        pos += size;
    }
    return SwByteArray(out.data(), out.size());
}

bool writeFile(const std::string& path, const SwByteArray& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out.write(bytes.constData(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

// ---- demuxer tests (all platforms) ----------------------------------------------------------

void testMp4DemuxerH264() {
    const std::vector<std::vector<std::uint8_t> > samples = makeH264Samples();
    const SwByteArray file = buildTestMp4(samples, 0, 90000, 3600, 3600);

    SwMp4Demuxer demuxer;
    expect(demuxer.openMemory(file), "h264: openMemory succeeds");
    expect(demuxer.durationMs() == 200, "h264: movie duration is 200 ms");

    const std::vector<SwMp4Demuxer::Track> tracks = demuxer.tracks();
    expect(tracks.size() == 1, "h264: one track");
    const SwMp4Demuxer::Track* video = demuxer.videoTrack();
    if (!expect(video != nullptr, "h264: video track found")) {
        return;
    }
    expect(video->trackId == 1, "h264: track id 1");
    expect(video->kind == SwMp4Demuxer::TrackKind::Video, "h264: kind video");
    expect(video->videoCodec == SwVideoPacket::Codec::H264, "h264: codec H264");
    expect(video->codecName == "h264", "h264: codec name");
    expect(video->timescale == 90000, "h264: timescale");
    expect(video->width == 320 && video->height == 240, "h264: dimensions");
    expect(video->nalLengthSize == 4, "h264: NAL length size 4");
    expect(video->sampleCount == 5, "h264: sample count");
    expect(video->durationMs == 200, "h264: track duration 200 ms");

    // Key-frame prefix: start-code SPS + start-code PPS.
    std::vector<char> prefix;
    const char sc[4] = {0, 0, 0, 1};
    prefix.insert(prefix.end(), sc, sc + 4);
    prefix.insert(prefix.end(),
                  reinterpret_cast<const char*>(kSps),
                  reinterpret_cast<const char*>(kSps) + sizeof(kSps));
    prefix.insert(prefix.end(), sc, sc + 4);
    prefix.insert(prefix.end(),
                  reinterpret_cast<const char*>(kPps),
                  reinterpret_cast<const char*>(kPps) + sizeof(kPps));
    expect(sameBytes(video->keyFramePrefix, prefix.data(), prefix.size()),
           "h264: Annex-B parameter set prefix");

    // Samples: dts 0/40/80/120/160 ms, pts = dts + 40 ms, keys at 0 and 3.
    for (std::uint64_t i = 0; i < 5; ++i) {
        SwMp4Demuxer::Sample sample;
        if (!expect(demuxer.readSample(1, i, sample), "h264: readSample succeeds")) {
            return;
        }
        expect(sample.dtsMs == static_cast<std::int64_t>(i) * 40, "h264: sample dts");
        expect(sample.ptsMs == static_cast<std::int64_t>(i) * 40 + 40, "h264: sample pts");
        expect(sample.keyFrame == (i == 0 || i == 3), "h264: sample key flag");
        const SwByteArray expected = annexBExpected(samples[static_cast<std::size_t>(i)]);
        expect(sameBytes(sample.payload, expected.constData(),
                         static_cast<std::size_t>(expected.size())),
               "h264: Annex-B payload");
    }

    // Sample 4 carries two NALs -> two start codes.
    {
        SwMp4Demuxer::Sample sample;
        demuxer.readSample(1, 4, sample);
        int startCodes = 0;
        const char* p = sample.payload.constData();
        for (int i = 0; i + 4 <= sample.payload.size(); ++i) {
            if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) {
                ++startCodes;
            }
        }
        expect(startCodes == 2, "h264: multi-NAL sample has two start codes");
    }

    SwMp4Demuxer::Sample outOfRange;
    expect(!demuxer.readSample(1, 5, outOfRange), "h264: out-of-range sample rejected");
    expect(!demuxer.readSample(7, 0, outOfRange), "h264: unknown track rejected");

    // Sync-sample lookup (pts: 40/80/120/160/200, keys at samples 0 and 3).
    expect(demuxer.findSyncSampleAtOrBefore(1, 0) == 0, "h264: sync at 0");
    expect(demuxer.findSyncSampleAtOrBefore(1, 100) == 0, "h264: sync before second key");
    // Discriminates a pts-keyed index from a dts-keyed one: the second key has dts 120
    // but pts 160, so target 159 must still resolve to the first key.
    expect(demuxer.findSyncSampleAtOrBefore(1, 159) == 0, "h264: sync index keyed on pts");
    expect(demuxer.findSyncSampleAtOrBefore(1, 160) == 3, "h264: sync at second key");
    expect(demuxer.findSyncSampleAtOrBefore(1, 5000) == 3, "h264: sync past the end");
}

void testMp4DemuxerAv1() {
    std::vector<std::vector<std::uint8_t> > samples;
    for (int i = 0; i < 3; ++i) {
        std::vector<std::uint8_t> sample;
        for (int b = 0; b < 6 + i; ++b) {
            sample.push_back(static_cast<std::uint8_t>(0x30 + 0x10 * i + b));
        }
        samples.push_back(sample);
    }
    const SwByteArray file = buildTestMp4(samples, 1, 1000, 40, 0);

    SwMp4Demuxer demuxer;
    expect(demuxer.openMemory(file), "av1: openMemory succeeds");
    const SwMp4Demuxer::Track* video = demuxer.videoTrack();
    if (!expect(video != nullptr, "av1: video track found")) {
        return;
    }
    expect(video->videoCodec == SwVideoPacket::Codec::AV1, "av1: codec AV1");
    expect(video->nalLengthSize == 0, "av1: raw OBU payloads");
    expect(sameBytes(video->keyFramePrefix, kAv1ConfigObus, sizeof(kAv1ConfigObus)),
           "av1: config OBUs exposed as key-frame prefix");

    SwMp4Demuxer::Sample sample;
    expect(demuxer.readSample(1, 1, sample), "av1: readSample succeeds");
    expect(sample.dtsMs == 40 && sample.ptsMs == 40, "av1: timestamps without ctts");
    expect(sameBytes(sample.payload, samples[1].data(), samples[1].size()),
           "av1: payload passthrough");
}

void testLengthPrefixedNalWalker() {
    // Shared helper (SwHevcBitstream.h) used by both the MP4 demuxer and SwRtpPacketizer.
    const std::uint8_t twoNals[] = {0x00, 0x00, 0x00, 0x02, 0xAA, 0xBB,
                                    0x00, 0x00, 0x00, 0x01, 0xCC};
    std::vector<std::pair<std::size_t, std::size_t> > seen;
    expect(swForEachLengthPrefixedNalUnit(
               twoNals, sizeof(twoNals), 4,
               [&seen](const SwLengthPrefixedNalUnitView& nal) {
                   seen.push_back(std::make_pair(nal.offset, nal.size));
               }),
           "nal-walk: valid buffer parses");
    expect(seen.size() == 2 && seen[0].first == 4 && seen[0].second == 2 &&
               seen[1].first == 10 && seen[1].second == 1,
           "nal-walk: offsets and sizes");

    const std::uint8_t zeroLen[] = {0x00, 0x00, 0x00, 0x00, 0xAA};
    expect(!swForEachLengthPrefixedNalUnit(zeroLen, sizeof(zeroLen), 4, nullptr),
           "nal-walk: zero-length NAL rejected");

    const std::uint8_t overrun[] = {0x00, 0x00, 0x00, 0x09, 0xAA};
    expect(!swForEachLengthPrefixedNalUnit(overrun, sizeof(overrun), 4, nullptr),
           "nal-walk: overrunning length rejected");

    const std::uint8_t trailing[] = {0x00, 0x00, 0x00, 0x01, 0xAA, 0xFF};
    expect(!swForEachLengthPrefixedNalUnit(trailing, sizeof(trailing), 4, nullptr),
           "nal-walk: trailing bytes rejected");

    expect(!swForEachLengthPrefixedNalUnit(twoNals, 0, 4, nullptr),
           "nal-walk: empty buffer rejected");

    const std::uint8_t twoByteLen[] = {0x00, 0x03, 0xAA, 0xBB, 0xCC};
    seen.clear();
    expect(swForEachLengthPrefixedNalUnit(
               twoByteLen, sizeof(twoByteLen), 2,
               [&seen](const SwLengthPrefixedNalUnitView& nal) {
                   seen.push_back(std::make_pair(nal.offset, nal.size));
               }),
           "nal-walk: 2-byte lengths parse");
    expect(seen.size() == 1 && seen[0].first == 2 && seen[0].second == 3,
           "nal-walk: 2-byte length view");
}

void testMp4DemuxerSeekIndex() {
    // 300 samples, key frame every 25 samples: the precomputed sync index must resolve
    // seeks exactly like the previous linear scan, in O(log n).
    std::vector<std::vector<std::uint8_t> > samples;
    std::vector<std::uint32_t> syncSamples;
    for (int i = 0; i < 300; ++i) {
        std::vector<std::uint8_t> sample;
        sample.push_back(0);
        sample.push_back(0);
        sample.push_back(0);
        sample.push_back(2);
        sample.push_back((i % 25) == 0 ? 0x65 : 0x41);
        sample.push_back(static_cast<std::uint8_t>(i));
        samples.push_back(sample);
        if ((i % 25) == 0) {
            syncSamples.push_back(static_cast<std::uint32_t>(i + 1)); // stss is 1-based
        }
    }
    const SwByteArray file = buildTestMp4(samples, 0, 1000, 40, 0, &syncSamples);
    SwMp4Demuxer demuxer;
    if (!expect(demuxer.openMemory(file), "seek-index: openMemory succeeds")) {
        return;
    }
    expect(demuxer.sampleCount(1) == 300, "seek-index: sample count");
    // Keys at indexes 0, 25, 50, ... with pts = index * 40 ms.
    expect(demuxer.findSyncSampleAtOrBefore(1, 0) == 0, "seek-index: start");
    expect(demuxer.findSyncSampleAtOrBefore(1, 999) == 0, "seek-index: before second key");
    expect(demuxer.findSyncSampleAtOrBefore(1, 1000) == 25, "seek-index: exactly on a key");
    expect(demuxer.findSyncSampleAtOrBefore(1, 5480) == 125, "seek-index: mid-file");
    expect(demuxer.findSyncSampleAtOrBefore(1, 1000000) == 275, "seek-index: past the end");
}

void testFactorySdpRouting() {
    // The SDP is parsed once and routes udp:// to the direct RTP source (passive ctor).
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl("udp://127.0.0.1:5004");
    options.sdpText =
        "v=0\r\n"
        "o=- 0 0 IN IP4 127.0.0.1\r\n"
        "s=Test\r\n"
        "c=IN IP4 127.0.0.1\r\n"
        "t=0 0\r\n"
        "m=video 5004 RTP/AVP 96\r\n"
        "a=rtpmap:96 H264/90000\r\n";
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(options);
    if (expect(source != nullptr, "sdp: source created")) {
        expect(source->name() == "SwDirectRtpMediaSource",
               "sdp: udp+SDP routes to the direct RTP source");
        // Pins that the SDP was actually APPLIED (not just parsed): the direct source
        // publishes its tracks from the description at construction time.
        bool sdpVideoTrack = false;
        const SwList<SwMediaTrack> tracks = source->tracks();
        for (int i = 0; i < tracks.size(); ++i) {
            if (tracks[i].isVideo() && tracks[i].codec == "h264" &&
                tracks[i].payloadType == 96 && tracks[i].clockRate == 90000) {
                sdpVideoTrack = true;
            }
        }
        expect(sdpVideoTrack, "sdp: h264/96/90000 track applied from the SDP");
    }

    // Without SDP the plain UDP source is kept.
    SwMediaOpenOptions plain = SwMediaOpenOptions::fromUrl("udp://127.0.0.1:5004");
    std::shared_ptr<SwMediaSource> plainSource = SwMediaSourceFactory::createMediaSource(plain);
    if (expect(plainSource != nullptr, "sdp: plain udp source created")) {
        expect(plainSource->name() == "SwUdpVideoSource",
               "sdp: plain udp keeps the raw UDP source");
    }
}

void testMp4DemuxerNegativeCtts() {
    // ffmpeg -movflags negative_cts_offsets writes version-1 ctts entries; a naive
    // unsigned dts+offset sum would wrap to ~2^64 and corrupt every pts.
    const std::vector<std::vector<std::uint8_t> > samples = makeH264Samples();
    const SwByteArray file = buildTestMp4(samples, 0, 90000, 3600, -3600);

    SwMp4Demuxer demuxer;
    expect(demuxer.openMemory(file), "ctts-neg: openMemory succeeds");
    for (std::uint64_t i = 0; i < 5; ++i) {
        SwMp4Demuxer::Sample sample;
        expect(demuxer.readSample(1, i, sample), "ctts-neg: readSample succeeds");
        const std::int64_t expectedPts =
            static_cast<std::int64_t>(i) * 40 - 40; // clamped at 0 for the first sample
        expect(sample.ptsMs == (expectedPts < 0 ? 0 : expectedPts),
               "ctts-neg: pts clamped, no unsigned wrap");
        expect(sample.dtsMs == static_cast<std::int64_t>(i) * 40, "ctts-neg: dts intact");
    }
    expect(demuxer.findSyncSampleAtOrBefore(1, 0) == 0, "ctts-neg: sync lookup sane");
}

void testMp4DemuxerErrors() {
    SwMp4Demuxer demuxer;
    expect(!demuxer.openMemory(SwByteArray()), "errors: empty buffer rejected");

    SwByteArray garbage("this is definitely not an mp4 file at all!", 43);
    expect(!demuxer.openMemory(garbage), "errors: garbage rejected");

    // ftyp + moof => fragmented, must be rejected with a clear error.
    Mp4Writer w;
    std::size_t box = w.boxBegin("ftyp");
    w.bytes("isom", 4);
    w.u32(0);
    w.boxEnd(box);
    box = w.boxBegin("moof");
    w.boxEnd(box);
    expect(!demuxer.openMemory(w.toByteArray()), "errors: fragmented mp4 rejected");
    expect(demuxer.errorText().find("fragmented") != std::string::npos,
           "errors: fragmented error text");
}

void testMp4DemuxerFromDisk(const std::string& path) {
    const std::vector<std::vector<std::uint8_t> > samples = makeH264Samples();
    const SwByteArray file = buildTestMp4(samples, 0, 90000, 3600, 3600);
    if (!expect(writeFile(path, file), "disk: temp mp4 written")) {
        return;
    }
    SwMp4Demuxer demuxer;
    expect(demuxer.openFile(path), "disk: openFile succeeds");
    expect(demuxer.sampleCount(1) == 5, "disk: sample count");
    SwMp4Demuxer::Sample sample;
    expect(demuxer.readSample(1, 3, sample), "disk: readSample succeeds");
    expect(sample.keyFrame && sample.dtsMs == 120, "disk: sample 3 is the second key frame");
}

void testFactoryRouting(const std::string& path) {
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(
        SwString(path.c_str()));
    if (!expect(source != nullptr, "factory: source created for .mp4 path")) {
        return;
    }
    std::shared_ptr<SwVideoSource> video = std::dynamic_pointer_cast<SwVideoSource>(source);
    expect(video != nullptr, "factory: source is a video source");
    expect(std::dynamic_pointer_cast<SwMediaTimelineSource>(source) != nullptr,
           "factory: movie source exposes the timeline interface");
#if !defined(_WIN32)
    expect(source->name() == "SwMp4MovieSource",
           "factory: non-Windows routes to SwMp4MovieSource");
#endif

    std::shared_ptr<SwMediaSource> srtSource =
        SwMediaSourceFactory::createMediaSource(SwString("srt://127.0.0.1:9710"));
    if (expect(srtSource != nullptr, "factory: srt source created")) {
        expect(srtSource->name() == "SwSrtVideoSource",
               "factory: srt:// routes to SwSrtVideoSource");
    }
}

void testBmffSniffRouting(const std::string& annexBPath) {
    // A raw Annex-B elementary stream merely named .mp4 must keep the raw-file path
    // (it played through SwFileVideoSource before container support existed).
    const std::uint8_t annexB[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E,
                                   0x00, 0x00, 0x00, 0x01, 0x65, 0x11, 0x22, 0x33};
    expect(writeFile(annexBPath,
                     SwByteArray(reinterpret_cast<const char*>(annexB), sizeof(annexB))),
           "sniff: annex-b temp file written");
    expect(!SwMp4Demuxer::looksLikeBmff(annexBPath), "sniff: annex-b is not BMFF");
#if !defined(_WIN32)
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(
        SwString(annexBPath.c_str()));
    if (expect(source != nullptr, "sniff: source created")) {
        expect(source->name() == "SwFileVideoSource",
               "sniff: annex-b .mp4 falls back to the raw file source");
    }
#endif
}

// ---- movie source runtime tests (all platforms — the MP4 source is neutral) -----------------

struct PacketLog {
    std::mutex mutex;
    std::vector<SwVideoPacket> packets;

    void push(const SwVideoPacket& packet) {
        std::lock_guard<std::mutex> lock(mutex);
        packets.push_back(packet);
    }
    std::size_t count() {
        std::lock_guard<std::mutex> lock(mutex);
        return packets.size();
    }
    SwVideoPacket at(std::size_t index) {
        std::lock_guard<std::mutex> lock(mutex);
        return packets[index];
    }
    std::vector<SwVideoPacket> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return packets;
    }
};

bool waitForStopped(SwMediaSource& source, int timeoutMs) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (source.streamStatus().state == SwMediaSource::StreamState::Stopped) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void testMovieSourcePlayback(const std::string& path) {
    SwMp4MovieSource source(path);
    if (!expect(source.initialize(), "movie: initialize succeeds")) {
        return;
    }
    expect(source.isSeekable(), "movie: seekable");
    expect(source.durationMs() == 200, "movie: duration 200 ms");
    expect(source.tracks().size() == 1, "movie: one published track");

    PacketLog log;
    source.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    source.start();
    expect(waitForStopped(source, 10000), "movie: playback completes");
    const std::int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

    const std::vector<SwVideoPacket> packets = log.snapshot();
    if (!expect(packets.size() == 5, "movie: five packets emitted")) {
        source.stop();
        return;
    }
    expect(packets[0].codec() == SwVideoPacket::Codec::H264, "movie: H264 packets");
    expect(packets[0].isKeyFrame(), "movie: first packet is a key frame");
    expect(!packets[1].isKeyFrame(), "movie: second packet is not a key frame");
    for (std::size_t i = 0; i < packets.size(); ++i) {
        expect(packets[i].dts() == static_cast<std::int64_t>(i) * 40, "movie: packet dts");
        expect(packets[i].pts() == static_cast<std::int64_t>(i) * 40 + 40, "movie: packet pts");
    }
    // Key frames start with the Annex-B SPS (00 00 00 01 67 ...).
    const char* first = packets[0].payload().constData();
    expect(packets[0].payload().size() > 5 && first[0] == 0 && first[1] == 0 && first[2] == 0 &&
               first[3] == 1 && static_cast<unsigned char>(first[4]) == 0x67,
           "movie: key frame carries the parameter sets");
    expect(elapsedMs >= 100, "movie: playback is paced in real time");
    expect(source.positionMs() == 200, "movie: final position");

    // Replay after natural end-of-file: start() must reap the finished worker (this used
    // to std::terminate) and restart from the beginning since the position is at the end.
    source.start();
    expect(waitForStopped(source, 10000), "movie: replay after EOF completes");
    expect(log.count() == 10, "movie: replay emitted the five samples again");
    source.stop();
}

void testMovieSourceSeek(const std::string& path) {
    SwMp4MovieSource source(path);
    if (!expect(source.initialize(), "seek: initialize succeeds")) {
        return;
    }
    PacketLog log;
    source.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });
    source.start();

    // Wait for the first packet, then jump to the second key frame (pts 160 ms -> dts 120 ms).
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (log.count() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!expect(log.count() > 0, "seek: playback started")) {
        source.stop();
        return;
    }
    expect(source.seek(160), "seek: request accepted");
    expect(waitForStopped(source, 10000), "seek: playback completes after seek");

    const std::vector<SwVideoPacket> packets = log.snapshot();
    bool foundResume = false;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        if (packets[i].isDiscontinuity()) {
            expect(packets[i].dts() == 120, "seek: resumes at the sync sample");
            expect(packets[i].isKeyFrame(), "seek: resumes on a key frame");
            foundResume = true;
            break;
        }
    }
    expect(foundResume, "seek: discontinuity packet emitted");
    source.stop();
}

void testMovieSourceSeekWhileStopped(const std::string& path) {
    // seek() before start() must be honoured (SwMediaPlayer's seek-then-play pattern).
    SwMp4MovieSource source(path);
    if (!expect(source.initialize(), "cold-seek: initialize succeeds")) {
        return;
    }
    PacketLog log;
    source.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });
    expect(source.seek(160), "cold-seek: request accepted while stopped");
    source.start();
    expect(waitForStopped(source, 10000), "cold-seek: playback completes");
    const std::vector<SwVideoPacket> packets = log.snapshot();
    if (expect(!packets.empty(), "cold-seek: packets emitted")) {
        expect(packets[0].dts() == 120 && packets[0].isKeyFrame(),
               "cold-seek: playback starts at the sync sample");
        expect(packets.size() == 2, "cold-seek: only the tail samples play");
    }
    source.stop();
}

#if defined(__linux__)

// ---- Linux-only runtime tests ---------------------------------------------------------------

void testV4l2Source() {
    SwLinuxVideoSource source(0);
    expect(source.name() == "SwLinuxVideoSource", "v4l2: name");
    const bool available = source.initialize();
    if (!available) {
        std::cout << "[LinuxMediaBackendsSelfTest] v4l2: no capture device, graceful failure OK"
                  << std::endl;
        return;
    }
    std::cout << "[LinuxMediaBackendsSelfTest] v4l2: device available, "
              << source.frameWidth() << "x" << source.frameHeight() << std::endl;
    PacketLog log;
    source.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });
    source.start();
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (log.count() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    source.stop();
    std::cout << "[LinuxMediaBackendsSelfTest] v4l2: captured " << log.count() << " frame(s)"
              << std::endl;
}

void testAlsaSink() {
    const bool available = SwAlsaAudioSink::runtimeAvailable();
    SwAlsaAudioSink sink;
    const bool opened = sink.open(48000, 2);
    if (!available) {
        expect(!opened, "alsa: open fails when libasound is missing");
        SwAudioFrame frame;
        expect(!sink.pushFrame(frame), "alsa: pushFrame fails when closed");
        std::cout << "[LinuxMediaBackendsSelfTest] alsa: libasound absent, graceful failure OK"
                  << std::endl;
    } else if (!opened) {
        std::cout << "[LinuxMediaBackendsSelfTest] alsa: library present but no playback device"
                  << std::endl;
    } else {
        SwAudioFrame frame;
        frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
        frame.setSampleRate(48000);
        frame.setChannelCount(2);
        frame.setTimestamp(0);
        frame.setPayload(SwByteArray(static_cast<std::size_t>(48000 / 50) * 2 * sizeof(float),
                                     '\0')); // 20 ms of silence
        expect(sink.pushFrame(frame), "alsa: pushFrame succeeds");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        sink.flush();
        sink.close();
        std::cout << "[LinuxMediaBackendsSelfTest] alsa: playback path exercised" << std::endl;
    }

    // Default sink selection follows the device probe (library present but no sound card,
    // e.g. containers/WSL, must fall back to the null sink instead of failing per packet).
    SwAudioOutput output;
    if (SwAlsaAudioSink::defaultDeviceUsable()) {
        expect(output.sinkName() == "SwAlsaAudioSink", "alsa: default output sink is ALSA");
    } else {
        expect(output.sinkName() == "SwNullAudioSink", "alsa: default output falls back to null");
    }
}

void testOpusDecoder() {
    const bool available = SwOpusAudioDecoder::runtimeAvailable();
    std::shared_ptr<SwAudioDecoder> decoder =
        SwAudioDecoderFactory::instance().acquire(SwAudioPacket::Codec::Opus);
    if (!available) {
        expect(decoder == nullptr, "opus: factory yields no decoder without libopus");
        std::cout << "[LinuxMediaBackendsSelfTest] opus: libopus absent, graceful failure OK"
                  << std::endl;
        return;
    }
    if (!expect(decoder != nullptr, "opus: factory yields a decoder")) {
        return;
    }
    // Minimal valid Opus packet: a lone TOC byte (CELT fullband, 20 ms, mono, code 0)
    // with an empty frame -> decodes to 960 samples of silence at 48 kHz.
    SwAudioPacket packet;
    packet.setCodec(SwAudioPacket::Codec::Opus);
    const char toc = static_cast<char>(0xF8);
    packet.setPayload(SwByteArray(&toc, 1));
    packet.setSampleRate(48000);
    packet.setChannelCount(1);
    packet.setPts(1234);
    SwAudioFrame frame;
    expect(decoder->decode(packet, frame), "opus: decode succeeds");
    expect(frame.isValid(), "opus: frame valid");
    expect(frame.sampleRate() == 48000, "opus: frame sample rate");
    expect(frame.channelCount() == 1, "opus: frame channels");
    expect(frame.sampleCount() == 960, "opus: 20 ms -> 960 samples");
    expect(frame.timestamp() == 1234, "opus: timestamp forwarded");
    std::cout << "[LinuxMediaBackendsSelfTest] opus: libopus decode path exercised" << std::endl;
}

#endif // __linux__

} // namespace

int main() {
    std::cout << "[LinuxMediaBackendsSelfTest] starting" << std::endl;

    const std::string tempPath = "sw95_selftest.mp4";
    const std::string annexBPath = "sw95_annexb.mp4";

    testLengthPrefixedNalWalker();
    testMp4DemuxerH264();
    testMp4DemuxerAv1();
    testMp4DemuxerNegativeCtts();
    testMp4DemuxerSeekIndex();
    testMp4DemuxerErrors();
    testMp4DemuxerFromDisk(tempPath);
    testFactoryRouting(tempPath);
    testFactorySdpRouting();
    testBmffSniffRouting(annexBPath);
    testMovieSourcePlayback(tempPath);
    testMovieSourceSeek(tempPath);
    testMovieSourceSeekWhileStopped(tempPath);

#if defined(__linux__)
    testV4l2Source();
    testAlsaSink();
    testOpusDecoder();
#endif

    std::remove(tempPath.c_str());
    std::remove(annexBPath.c_str());

    if (g_failures != 0) {
        std::cerr << "[LinuxMediaBackendsSelfTest] " << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "LinuxMediaBackendsSelfTest passed" << std::endl;
    return 0;
}
