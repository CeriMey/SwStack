/***************************************************************************************************
 * SrtMediaSelfTest
 *
 * Validates the SRT receive brick:
 *  - SwSrtLibrary: graceful behaviour whether or not libsrt is present (dlopen, zero deps).
 *  - SwSrtVideoSource URL handling: caller vs listener modes, streamid/latency options.
 *  - The MPEG-TS chain the source relies on: a crafted PAT/PMT/PES H264 stream fed through
 *    SwTsProgramDemux must publish an h264 track and emit video packets (all platforms).
 *  - End-to-end loopback when libsrt is available: a raw libsrt caller pushes the crafted
 *    TS stream to an SwSrtVideoSource in listener mode; the source must emit the demuxed
 *    video packets.
 ***************************************************************************************************/

#include "media/SwMediaOpenOptions.h"
#include "media/SwSrtLibrary.h"
#include "media/SwSrtVideoSource.h"
#include "media/rtp/SwTsMuxer.h"
#include "media/rtp/SwTsProgramDemux.h"
#include "media/server/SwMediaServerFactory.h"
#include "media/transport/SwSrtServerTransport.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[SrtMediaSelfTest] FAIL " << message << "\n";
        ++g_failures;
        return false;
    }
    return true;
}

// ---- minimal MPEG-TS writer ------------------------------------------------------------------

class TsWriter {
public:
    static const std::uint16_t kPmtPid = 0x0100;
    static const std::uint16_t kVideoPid = 0x0101;

    // One 188-byte TS packet; the payload is placed at the end, adaptation-field stuffing
    // fills the gap so no padding bytes leak into the elementary stream.
    void appendPacket(std::uint16_t pid,
                      bool payloadStart,
                      const std::vector<std::uint8_t>& payload) {
        std::vector<std::uint8_t> packet;
        packet.reserve(188);
        packet.push_back(0x47);
        packet.push_back(static_cast<std::uint8_t>((payloadStart ? 0x40 : 0x00) |
                                                   ((pid >> 8) & 0x1F)));
        packet.push_back(static_cast<std::uint8_t>(pid & 0xFF));

        const std::size_t space = 188 - 4;
        const std::size_t stuffing = space - payload.size(); // payload must fit
        if (stuffing == 0) {
            packet.push_back(static_cast<std::uint8_t>(0x10 | (m_continuity[pid] & 0x0F)));
        } else {
            packet.push_back(static_cast<std::uint8_t>(0x30 | (m_continuity[pid] & 0x0F)));
            packet.push_back(static_cast<std::uint8_t>(stuffing - 1)); // adaptation length
            if (stuffing >= 2) {
                packet.push_back(0x00); // adaptation flags
                for (std::size_t i = 2; i < stuffing; ++i) {
                    packet.push_back(0xFF);
                }
            }
        }
        packet.insert(packet.end(), payload.begin(), payload.end());
        m_continuity[pid] = static_cast<std::uint8_t>((m_continuity[pid] + 1) & 0x0F);
        m_bytes.insert(m_bytes.end(), packet.begin(), packet.end());
    }

    void appendPat() {
        std::vector<std::uint8_t> section;
        section.push_back(0x00); // pointer_field
        section.push_back(0x00); // table_id PAT
        section.push_back(0xB0);
        section.push_back(13); // section_length
        section.push_back(0x00);
        section.push_back(0x01); // transport_stream_id
        section.push_back(0xC1); // version/current_next
        section.push_back(0x00); // section_number
        section.push_back(0x00); // last_section_number
        section.push_back(0x00);
        section.push_back(0x01); // program_number 1
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kPmtPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kPmtPid & 0xFF));
        section.insert(section.end(), 4, 0x00); // CRC32 (not verified by the demux)
        appendPacket(0x0000, true, section);
    }

    void appendPmt() {
        std::vector<std::uint8_t> section;
        section.push_back(0x00); // pointer_field
        section.push_back(0x02); // table_id PMT
        section.push_back(0xB0);
        section.push_back(18); // section_length
        section.push_back(0x00);
        section.push_back(0x01); // program_number
        section.push_back(0xC1);
        section.push_back(0x00);
        section.push_back(0x00);
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kVideoPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kVideoPid & 0xFF)); // PCR PID
        section.push_back(0xF0);
        section.push_back(0x00); // program_info_length 0
        section.push_back(0x1B); // stream_type H264
        section.push_back(static_cast<std::uint8_t>(0xE0 | ((kVideoPid >> 8) & 0x1F)));
        section.push_back(static_cast<std::uint8_t>(kVideoPid & 0xFF));
        section.push_back(0xF0);
        section.push_back(0x00); // es_info_length 0
        section.insert(section.end(), 4, 0x00); // CRC32
        appendPacket(0x0000 | 0x0100, true, section);
    }

    // One PES packet carrying a single-NAL Annex-B access unit.
    void appendVideoFrame(std::uint64_t pts90k, bool keyFrame, std::uint8_t seed) {
        std::vector<std::uint8_t> pes;
        pes.push_back(0x00);
        pes.push_back(0x00);
        pes.push_back(0x01);
        pes.push_back(0xE0); // video stream id
        pes.push_back(0x00);
        pes.push_back(0x00); // PES length 0 (unbounded, standard for video)
        pes.push_back(0x80); // marker bits
        pes.push_back(0x80); // PTS only
        pes.push_back(0x05); // header length
        pes.push_back(static_cast<std::uint8_t>(0x21 | (((pts90k >> 30) & 0x07) << 1)));
        pes.push_back(static_cast<std::uint8_t>((pts90k >> 22) & 0xFF));
        pes.push_back(static_cast<std::uint8_t>(0x01 | (((pts90k >> 15) & 0x7F) << 1)));
        pes.push_back(static_cast<std::uint8_t>((pts90k >> 7) & 0xFF));
        pes.push_back(static_cast<std::uint8_t>(0x01 | ((pts90k & 0x7F) << 1)));
        // Annex-B access unit: one NAL (IDR 0x65 for key frames).
        pes.push_back(0x00);
        pes.push_back(0x00);
        pes.push_back(0x00);
        pes.push_back(0x01);
        pes.push_back(keyFrame ? 0x65 : 0x41);
        for (int i = 0; i < 24; ++i) {
            pes.push_back(static_cast<std::uint8_t>(seed + i));
        }
        appendPacket(kVideoPid, true, pes);
    }

    const std::vector<std::uint8_t>& bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
    std::uint8_t m_continuity[0x2000] = {0};
};

std::vector<std::uint8_t> buildTsStream(int frames) {
    TsWriter writer;
    writer.appendPat();
    writer.appendPmt();
    for (int i = 0; i < frames; ++i) {
        writer.appendVideoFrame(static_cast<std::uint64_t>(i) * 3600, i == 0,
                                static_cast<std::uint8_t>(0x10 * (i + 1)));
    }
    return writer.bytes();
}

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
    std::vector<SwVideoPacket> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return packets;
    }
};

// ---- tests -----------------------------------------------------------------------------------

void testUrlHandling() {
    {
        SwSrtVideoSource caller(SwMediaOpenOptions::fromUrl(
            "srt://192.168.1.10:9710?streamid=cam1&latency=120"));
        expect(!caller.isListener(), "url: host+port is caller mode");
        expect(caller.streamId() == "cam1", "url: streamid parsed");
        expect(caller.latencyMs() == 120, "url: latency parsed");
        expect(caller.name() == "SwSrtVideoSource", "url: source name");
    }
    {
        SwSrtVideoSource listener(SwMediaOpenOptions::fromUrl("srt://:9710"));
        expect(listener.isListener(), "url: empty host is listener mode");
    }
    {
        SwSrtVideoSource forced(SwMediaOpenOptions::fromUrl(
            "srt://192.168.1.10:9710?mode=listener"));
        expect(forced.isListener(), "url: mode=listener overrides");
    }
}

void testGracefulWithoutLibrary() {
    if (SwSrtLibrary::instance().available()) {
        return; // covered by the E2E test instead
    }
    SwSrtVideoSource source(SwMediaOpenOptions::fromUrl("srt://127.0.0.1:9711"));
    source.start();
    expect(source.streamStatus().state == SwMediaSource::StreamState::Recovering,
           "graceful: start without libsrt reports Recovering");
    source.stop();
    std::cout << "[SrtMediaSelfTest] libsrt absent, graceful failure OK" << std::endl;
}

void testTsChain() {
    // The exact chain SwSrtVideoSource relies on, without any network.
    SwTsProgramDemux demux;
    std::vector<SwMediaPacket> videoPackets;
    SwList<SwMediaTrack> tracks;
    demux.setPacketCallback([&videoPackets](const SwMediaPacket& packet) {
        if (packet.type() == SwMediaPacket::Type::Video) {
            videoPackets.push_back(packet);
        }
    });
    demux.setTracksChangedCallback([&tracks](const SwList<SwMediaTrack>& changed) {
        tracks = changed;
    });

    const std::vector<std::uint8_t> stream = buildTsStream(4);
    expect(stream.size() % 188 == 0, "ts: aligned packets");
    demux.feed(stream.data(), stream.size(), 1);

    bool hasVideoTrack = false;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].isVideo() && tracks[i].codec == "h264") {
            hasVideoTrack = true;
        }
    }
    expect(hasVideoTrack, "ts: h264 track published");
    // A PES packet is flushed by the next PUSI: 4 frames sent -> at least 3 emitted.
    if (expect(videoPackets.size() >= 3, "ts: video packets demuxed")) {
        expect(videoPackets[0].isKeyFrame(), "ts: first frame is the IDR");
        expect(!videoPackets[1].isKeyFrame(), "ts: second frame is not a key frame");
    }
}

SwByteArray makeAnnexBAccessUnit(bool keyFrame, std::uint8_t seed, std::size_t bodyBytes) {
    std::vector<char> au;
    au.push_back(0);
    au.push_back(0);
    au.push_back(0);
    au.push_back(1);
    au.push_back(static_cast<char>(keyFrame ? 0x65 : 0x41));
    for (std::size_t i = 0; i < bodyBytes; ++i) {
        au.push_back(static_cast<char>(seed + (i & 0x3F)));
    }
    return SwByteArray(au.data(), au.size());
}

void testTsMuxerRoundtrip() {
    // Our muxer's output must demux back through our own TS demuxer, byte-exact —
    // including a large access unit spanning several TS packets.
    SwTsMuxer muxer;
    std::vector<char> ts;
    std::vector<SwByteArray> accessUnits;
    accessUnits.push_back(makeAnnexBAccessUnit(true, 0x10, 1000)); // spans >5 TS packets
    accessUnits.push_back(makeAnnexBAccessUnit(false, 0x20, 40));
    accessUnits.push_back(makeAnnexBAccessUnit(false, 0x30, 200));
    accessUnits.push_back(makeAnnexBAccessUnit(true, 0x40, 60));
    for (std::size_t i = 0; i < accessUnits.size(); ++i) {
        muxer.muxAccessUnit(accessUnits[i], static_cast<std::int64_t>(i) * 40,
                            i == 0 || i == 3, ts);
    }
    expect(!ts.empty() && ts.size() % 188 == 0, "mux: 188-byte aligned output");

    SwTsProgramDemux demux;
    std::vector<SwMediaPacket> videoPackets;
    demux.setPacketCallback([&videoPackets](const SwMediaPacket& packet) {
        if (packet.type() == SwMediaPacket::Type::Video) {
            videoPackets.push_back(packet);
        }
    });
    demux.feed(reinterpret_cast<const std::uint8_t*>(ts.data()), ts.size(), 1);

    // A PES flushes at the next payload-unit-start: 4 in, at least 3 out.
    if (!expect(videoPackets.size() >= 3, "mux: demuxed video packets")) {
        return;
    }
    for (std::size_t i = 0; i < videoPackets.size() && i < accessUnits.size(); ++i) {
        const SwByteArray& expected = accessUnits[i];
        const SwByteArray& actual = videoPackets[i].payload();
        expect(actual.size() == expected.size() &&
                   std::memcmp(actual.constData(), expected.constData(),
                               static_cast<std::size_t>(expected.size())) == 0,
               "mux: access unit round-trips byte-exact");
    }
    expect(videoPackets[0].isKeyFrame(), "mux: key frame flag survives");
    expect(!videoPackets[1].isKeyFrame(), "mux: non-key flag survives");
}

void testServerTransportFactory() {
    std::shared_ptr<SwVideoTransportServer> transport =
        SwMediaServerFactory::createTransport(SwString("srt://127.0.0.1:19703"));
    if (expect(transport != nullptr, "server-factory: srt transport created")) {
        expect(transport->protocolName() == "srt", "server-factory: protocol name");
    }
}

void testSrtServerToSource() {
    SwSrtLibrary& srt = SwSrtLibrary::instance();
    if (!srt.available() || !srt.ensureStarted()) {
        return; // library-absent path already covered
    }

    const int port = 19703;
    SwSrtServerTransport server;
    SwMediaServerConfig config;
    config.endpoint.protocol = SwMediaTransportProtocol::Srt;
    config.endpoint.bindAddress = "127.0.0.1";
    config.endpoint.port = static_cast<uint16_t>(port);
    server.configure(config);

    SwVideoPublishStream stream;
    stream.codec = SwVideoPacket::Codec::H264;
    expect(server.addStream(stream), "server: stream added");
    if (!expect(server.start(), "server: listener started")) {
        return;
    }

    SwSrtVideoSource receiver(SwMediaOpenOptions::fromUrl(
        SwString(("srt://127.0.0.1:" + std::to_string(port) + "?latency=50").c_str())));
    expect(!receiver.isListener(), "server: receiver connects as caller");
    PacketLog log;
    receiver.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });
    receiver.start();

    // Wait for the receiver to connect, then publish frames (key frame first).
    const std::chrono::steady_clock::time_point connectDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (server.clientCount() == 0 &&
           std::chrono::steady_clock::now() < connectDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!expect(server.clientCount() > 0, "server: receiver connected")) {
        receiver.stop();
        server.stop();
        return;
    }

    const std::chrono::steady_clock::time_point publishDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    int frame = 0;
    while (log.count() < 5 && std::chrono::steady_clock::now() < publishDeadline) {
        SwVideoPacket packet(SwVideoPacket::Codec::H264,
                             makeAnnexBAccessUnit(frame % 10 == 0,
                                                  static_cast<std::uint8_t>(frame),
                                                  120),
                             frame * 33,
                             frame * 33,
                             frame % 10 == 0);
        packet.setClockRate(1000);
        server.publishVideoPacket("video", packet);
        ++frame;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    receiver.stop();

    const std::vector<SwVideoPacket> packets = log.snapshot();
    if (expect(packets.size() >= 5, "server: packets received end to end")) {
        expect(packets[0].codec() == SwVideoPacket::Codec::H264, "server: H264 codec");
        expect(packets[0].isKeyFrame(), "server: stream starts on a key frame");
    }
    const SwVideoServerMetrics metrics = server.metrics();
    expect(metrics.framesSent > 0, "server: metrics track sent frames");
    server.stop();
    std::cout << "[SrtMediaSelfTest] server e2e: " << packets.size()
              << " packet(s), " << frame << " published" << std::endl;
}

void testSrtLoopback() {
    SwSrtLibrary& srt = SwSrtLibrary::instance();
    if (!srt.available()) {
        return; // graceful path tested above
    }
    if (!srt.ensureStarted()) {
        expect(false, "e2e: srt_startup failed");
        return;
    }

    const int port = 19702;
    SwSrtVideoSource source(SwMediaOpenOptions::fromUrl(
        SwString(("srt://:" + std::to_string(port) + "?latency=50").c_str())));
    expect(source.isListener(), "e2e: source is in listener mode");

    PacketLog log;
    source.setPacketCallback([&log](const SwVideoPacket& packet) { log.push(packet); });
    source.start();

    // Raw libsrt caller pushing the crafted TS stream in 7-packet (1316-byte) chunks.
    std::thread sender([port, &srt]() {
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo* resolved = nullptr;
        if (::getaddrinfo("127.0.0.1", std::to_string(port).c_str(), &hints, &resolved) != 0 ||
            !resolved) {
            return;
        }
        int socket = SwSrtLibrary::kInvalidSocket;
        // The listener needs a moment to bind; retry the connect a few times.
        for (int attempt = 0; attempt < 20; ++attempt) {
            socket = srt.createSocket();
            if (socket == SwSrtLibrary::kInvalidSocket) {
                break;
            }
            const int connectTimeoutMs = 1000;
            srt.setSockFlag(socket, SwSrtLibrary::kOptConnTimeout, &connectTimeoutMs,
                            sizeof(connectTimeoutMs));
            if (srt.connect(socket, resolved->ai_addr,
                            static_cast<int>(resolved->ai_addrlen)) != SwSrtLibrary::kError) {
                break;
            }
            srt.close(socket);
            socket = SwSrtLibrary::kInvalidSocket;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        ::freeaddrinfo(resolved);
        if (socket == SwSrtLibrary::kInvalidSocket) {
            return;
        }
        const std::vector<std::uint8_t> stream = buildTsStream(6);
        for (std::size_t offset = 0; offset < stream.size(); offset += 1316) {
            const std::size_t chunk =
                (std::min)(static_cast<std::size_t>(1316), stream.size() - offset);
            srt.sendMsg(socket,
                        reinterpret_cast<const char*>(stream.data() + offset),
                        static_cast<int>(chunk),
                        -1,
                        1);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Leave the connection up long enough for delivery, then hang up.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        srt.close(socket);
    });

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (log.count() < 5 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    sender.join();
    source.stop();

    const std::vector<SwVideoPacket> packets = log.snapshot();
    if (expect(packets.size() >= 5, "e2e: video packets received over SRT")) {
        expect(packets[0].codec() == SwVideoPacket::Codec::H264, "e2e: H264 codec");
        expect(packets[0].isKeyFrame(), "e2e: IDR flagged as key frame");
    }
    std::cout << "[SrtMediaSelfTest] e2e: " << packets.size()
              << " video packet(s) over SRT loopback" << std::endl;
}

} // namespace

int main() {
    std::cout << "[SrtMediaSelfTest] starting (libsrt "
              << (SwSrtLibrary::instance().available() ? "available" : "absent") << ")"
              << std::endl;

    testUrlHandling();
    testGracefulWithoutLibrary();
    testTsChain();
    testTsMuxerRoundtrip();
    testServerTransportFactory();
    testSrtLoopback();
    testSrtServerToSource();

    if (g_failures != 0) {
        std::cerr << "[SrtMediaSelfTest] " << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "SrtMediaSelfTest passed" << std::endl;
    return 0;
}
