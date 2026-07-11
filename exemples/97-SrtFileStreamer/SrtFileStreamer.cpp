/***************************************************************************************************
 * SrtFileStreamer
 *
 * Live-streams an MP4 file over SRT using the SwStack media bricks end to end:
 *
 *   SwMp4MovieSource (native demuxer, real-time pacing, loop)
 *       -> SwSrtServerTransport via SwMediaServerFactory (MPEG-TS mux, SRT listener)
 *           -> any SRT player:  ffplay 'srt://127.0.0.1:9710?latency=100'
 *                               vlc    'srt://127.0.0.1:9710'
 *                               or SwSrtVideoSource / SwMediaPlayer with the same URL.
 *
 * Usage: SrtFileStreamer <file.mp4> [srt-listen-url]   (default srt://0.0.0.0:9710)
 ***************************************************************************************************/

#include "media/SwMp4MovieSource.h"
#include "media/server/SwMediaServerFactory.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t g_stopRequested = 0;
void handleSignal(int) {
    g_stopRequested = 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: SrtFileStreamer <file.mp4> [srt-listen-url]\n"
                  << "example: SrtFileStreamer clip.mp4 srt://0.0.0.0:9710\n"
                  << "then:    ffplay -fflags nobuffer 'srt://127.0.0.1:9710?latency=100'"
                  << std::endl;
        return 2;
    }
    const std::string filePath = argv[1];
    const std::string url = argc > 2 ? argv[2] : "srt://0.0.0.0:9710";

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    SwMp4MovieSource source(filePath);
    if (!source.initialize()) {
        std::cerr << "[SrtFileStreamer] cannot open " << filePath << std::endl;
        return 1;
    }
    const SwList<SwMediaTrack> tracks = source.tracks();
    SwString codecName = tracks.size() > 0 ? tracks[0].codec : SwString("h264");
    std::cout << "[SrtFileStreamer] " << filePath << ": codec=" << codecName
              << " duration=" << source.durationMs() << "ms" << std::endl;

    std::shared_ptr<SwVideoTransportServer> transport =
        SwMediaServerFactory::createTransport(SwString(url.c_str()));
    if (!transport) {
        std::cerr << "[SrtFileStreamer] unsupported URL: " << url << std::endl;
        return 1;
    }
    SwVideoPublishStream stream;
    stream.codec = (codecName == "h265") ? SwVideoPacket::Codec::H265
                                         : SwVideoPacket::Codec::H264;
    transport->addStream(stream);
    if (!transport->start()) {
        std::cerr << "[SrtFileStreamer] transport start failed (libsrt available? port free?)"
                  << std::endl;
        return 1;
    }
    std::cout << "[SrtFileStreamer] listening on " << url
              << " (protocol=" << transport->protocolName() << ")" << std::endl;

    source.setLoop(true);
    source.setPacketCallback([&transport](const SwVideoPacket& packet) {
        transport->publishVideoPacket("video", packet);
    });
    source.start();

    std::uint64_t lastFramesSent = 0;
    while (!g_stopRequested) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const SwVideoServerMetrics metrics = transport->metrics();
        std::cout << "[SrtFileStreamer] framesSent=" << metrics.framesSent
                  << " (+" << (metrics.framesSent - lastFramesSent) << ")"
                  << " bytesSent=" << metrics.transport.bytesSent
                  << " sendFailures=" << metrics.transport.sendFailures
                  << " position=" << source.positionMs() << "ms" << std::endl;
        lastFramesSent = metrics.framesSent;
    }

    source.stop();
    transport->stop();
    std::cout << "[SrtFileStreamer] stopped" << std::endl;
    return 0;
}
