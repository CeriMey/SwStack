#include "media/SwMediaSourceFactory.h"
#include "media/SwMediaOpenOptions.h"
#include "media/SwSdpMediaDescription.h"
#include "media/rtp/SwRtpJitterBuffer.h"
#include "media/rtp/SwRtpSessionDescriptor.h"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool expect(bool condition, const char* scenario, const char* message) {
    if (condition) {
        return true;
    }
    std::cerr << "[" << scenario << "] FAIL " << message << "\n";
    return false;
}

std::string h265MulticastSdp() {
    return
        "v=0\r\n"
        "o=- 1 1 IN IP4 127.0.0.1\r\n"
        "s=Direct H265\r\n"
        "c=IN IP4 239.10.10.10/32\r\n"
        "m=video 5010 RTP/AVP 98\r\n"
        "a=rtpmap:98 H265/90000\r\n"
        "a=fmtp:98 sprop-vps=QAEMAf//AWAAAAMAsAAAAwAAAwB4wJ; sprop-sps=QgEBAWAAAAMAsAAAAwAAAwB4gA==; sprop-pps=RAHBcrRiQA==\r\n";
}

std::string mpegTsRtpSdp() {
    return
        "v=0\n"
        "o=- 1 1 IN IP4 127.0.0.1\n"
        "s=RTP MPEGTS\n"
        "m=video 5004 RTP/AVP 33\n";
}

std::string multiTrackSdp() {
    return
        "v=0\r\n"
        "o=- 1 1 IN IP4 127.0.0.1\r\n"
        "s=Direct multi-track\r\n"
        "c=IN IP4 239.20.20.20/32\r\n"
        "a=source-filter: incl IN IP4 239.20.20.20 192.168.10.50\r\n"
        "m=video 5004 RTP/AVP 96\r\n"
        "a=rtcp:5005 IN IP4 239.20.20.20\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=framerate:30\r\n"
        "a=ssrc:123456 cname:video\r\n"
        "m=audio 5006 RTP/AVP 0\r\n"
        "a=rtcp:5007\r\n"
        "a=rtpmap:0 PCMU/8000/1\r\n"
        "m=application 5008 RTP/AVP 98\r\n"
        "a=rtcp:5009\r\n"
        "a=rtpmap:98 smpte336m/90000\r\n";
}

bool runSdpApplyScenario() {
    const char* scenario = "SDP apply";
    SwSdpMediaDescription description;
    bool ok = expect(SwSdpMediaDescription::parse(h265MulticastSdp(), description),
                     scenario,
                     "parse returned false");
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl("udp://0.0.0.0");
    ok = expect(description.applyToOpenOptions(options), scenario, "apply returned false") && ok;
    ok = expect(options.sdpApplied, scenario, "sdpApplied is false") && ok;
    ok = expect(options.rtpPacketized, scenario, "rtpPacketized is false") && ok;
    ok = expect(options.rtpPort == 5010, scenario, "rtpPort was not loaded") && ok;
    ok = expect(options.rtcpPort == 5011, scenario, "rtcpPort was not derived") && ok;
    ok = expect(options.payloadType == 98, scenario, "payload type was not loaded") && ok;
    ok = expect(options.clockRate == 90000, scenario, "clock was not loaded") && ok;
    ok = expect(options.codec == SwVideoPacket::Codec::H265, scenario, "codec was not loaded") && ok;
    ok = expect(options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::Rtp,
                scenario,
                "format was not set to RTP") && ok;
    ok = expect(options.multicastGroup == "239.10.10.10",
                scenario,
                "multicast group was not loaded") && ok;
    ok = expect(!options.fmtp.isEmpty(), scenario, "fmtp was not loaded") && ok;
    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runMpegTsRtpScenario() {
    const char* scenario = "SDP RTP MP2T";
    SwSdpMediaDescription description;
    bool ok = expect(SwSdpMediaDescription::parse(mpegTsRtpSdp(), description),
                     scenario,
                     "parse returned false");
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl("udp://0.0.0.0");
    ok = expect(description.applyToOpenOptions(options), scenario, "apply returned false") && ok;
    ok = expect(options.rtpPacketized, scenario, "rtpPacketized is false") && ok;
    ok = expect(options.udpFormat == SwMediaOpenOptions::UdpPayloadFormat::MpegTs,
                scenario,
                "format was not set to MPEG-TS") && ok;
    ok = expect(options.payloadType == 33, scenario, "payload type was not loaded") && ok;
    ok = expect(options.rtpPort == 5004, scenario, "rtpPort was not loaded") && ok;
    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runFactoryRoutingScenario() {
    const char* scenario = "factory routing";
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl("udp://0.0.0.0");
    options.sdpText = SwString(mpegTsRtpSdp());
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(options);
    bool ok = expect(source && source->name() == "SwDirectRtpMediaSource",
                     scenario,
                     "SDP RTP/AVP over udp:// did not create SwDirectRtpMediaSource");

    SwMediaOpenOptions rawUdpOptions = SwMediaOpenOptions::fromUrl("udp://0.0.0.0:5004");
    std::shared_ptr<SwMediaSource> rawUdpSource =
        SwMediaSourceFactory::createMediaSource(rawUdpOptions);
    ok = expect(rawUdpSource && rawUdpSource->name() == "SwUdpVideoSource",
                scenario,
                "plain udp:// did not keep SwUdpVideoSource") && ok;

    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runMultiTrackSdpScenario() {
    const char* scenario = "multi-track SDP";
    SwSdpMediaDescription description;
    bool ok = expect(SwSdpMediaDescription::parse(multiTrackSdp(), description),
                     scenario,
                     "parse returned false");
    ok = expect(description.tracks.size() == 3U, scenario, "track count mismatch") && ok;
    ok = expect(description.tracks[0].rtcpPort == 5005,
                scenario,
                "video rtcp port was not parsed") && ok;
    ok = expect(description.tracks[0].sourceAddressFilter == "192.168.10.50",
                scenario,
                "session source-filter was not inherited") && ok;
    ok = expect(description.tracks[0].ssrc == 123456U,
                scenario,
                "ssrc was not parsed") && ok;
    ok = expect(description.tracks[0].frameRate > 29.9 && description.tracks[0].frameRate < 30.1,
                scenario,
                "framerate was not parsed") && ok;
    ok = expect(description.tracks[1].isAudio() &&
                description.tracks[1].clockRate == 8000 &&
                description.tracks[1].channelCount == 1,
                scenario,
                "audio rtpmap was not parsed") && ok;
    ok = expect(description.tracks[2].isMetadata() &&
                description.tracks[2].codecName == "smpte336m",
                scenario,
                "metadata track was not parsed") && ok;

    SwMediaOpenOptions options =
        SwMediaOpenOptions::fromUrl("udp://0.0.0.0?audio=1&metadata=1");
    options.sdpText = SwString(multiTrackSdp());
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(options);
    ok = expect(source && source->name() == "SwDirectRtpMediaSource",
                scenario,
                "factory did not create direct RTP source") && ok;
    if (source) {
        const SwList<SwMediaTrack> tracks = source->tracks();
        int videoCount = 0;
        int audioCount = 0;
        int metadataCount = 0;
        int selectedCount = 0;
        for (const auto& track : tracks) {
            if (track.isVideo()) ++videoCount;
            if (track.isAudio()) ++audioCount;
            if (track.isMetadata()) ++metadataCount;
            if (track.selected) ++selectedCount;
        }
        ok = expect(videoCount == 1, scenario, "video track was not published") && ok;
        ok = expect(audioCount == 1, scenario, "audio track was not published") && ok;
        ok = expect(metadataCount == 1, scenario, "metadata track was not published") && ok;
        ok = expect(selectedCount == 3, scenario, "enabled tracks were not selected") && ok;
    }
    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runSdpFileScenario() {
    const char* scenario = "file SDP routing";
    const char* fileName = "sw_sdp_options_selftest.sdp";
    {
        std::ofstream file(fileName, std::ios::out | std::ios::binary);
        file << multiTrackSdp();
    }
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl(SwString(fileName));
    options.enableAudio = true;
    options.enableMetadata = true;
    std::shared_ptr<SwMediaSource> source = SwMediaSourceFactory::createMediaSource(options);
    std::remove(fileName);
    bool ok = expect(source && source->name() == "SwDirectRtpMediaSource",
                     scenario,
                     "plain .sdp file path did not create direct RTP source");
    if (source) {
        ok = expect(source->tracks().size() >= 3U,
                    scenario,
                    "SDP file tracks were not published") && ok;
    }
    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runRtpDescriptorScenario() {
    const char* scenario = "RTP descriptor defaults";
    SwMediaOpenOptions options = SwMediaOpenOptions::fromUrl("rtp://127.0.0.1:5004");
    SwRtpSessionDescriptor descriptor = SwRtpSessionDescriptor::fromOpenOptions(options);
    bool ok = expect(descriptor.localRtpPort == 5004,
                     scenario,
                     "URL port was not used");
    ok = expect(descriptor.localRtcpPort == 5005,
                scenario,
                "RTCP port was not derived") && ok;
    ok = expect(descriptor.payloadType == -1,
                scenario,
                "unspecified payload type should accept any PT") && ok;
    ok = expect(descriptor.codec == SwVideoPacket::Codec::H264,
                scenario,
                "default codec should remain H264") && ok;

    SwMediaOpenOptions tunedOptions =
        SwMediaOpenOptions::fromUrl("rtp://127.0.0.1:5004?latency=1200&jitter=250&jitter_packets=512");
    SwRtpSessionDescriptor tunedDescriptor =
        SwRtpSessionDescriptor::fromOpenOptions(tunedOptions);
    ok = expect(tunedOptions.latencyTargetMs == 1200,
                scenario,
                "latency option was not parsed") && ok;
    ok = expect(tunedDescriptor.jitterMaxDelayMs == 250,
                scenario,
                "jitter delay option was not copied") && ok;
    ok = expect(tunedDescriptor.jitterMaxPackets == 512,
                scenario,
                "jitter packet option was not copied") && ok;
    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runRtpJitterBufferScenario() {
    const char* scenario = "RTP jitter buffer";
    SwRtpJitterBuffer jitter;
    jitter.setLimits(512, 20);
    const auto now = std::chrono::steady_clock::now();
    const SwByteArray packet("x");

    bool ok = expect(jitter.enqueue(1000, packet, now) ==
                         SwRtpJitterBuffer::InsertResult::AcceptedInOrder,
                     scenario,
                     "first packet was not accepted in order");
    SwRtpJitterBuffer::PopResult result = jitter.popReady(false);
    ok = expect(result.ready && !result.gapAdvanced && result.actualSequence == 1000,
                scenario,
                "in-order packet did not pop without gap advance") && ok;

    ok = expect(jitter.enqueue(1003,
                               packet,
                               now - std::chrono::milliseconds(50)) ==
                    SwRtpJitterBuffer::InsertResult::AcceptedOutOfOrder,
                scenario,
                "out-of-order packet was not buffered") && ok;
    result = jitter.popReady(false);
    ok = expect(!result.ready,
                scenario,
                "read-path pop advanced an aged gap without permission") && ok;
    result = jitter.popReady(true);
    ok = expect(result.ready &&
                    result.gapAdvanced &&
                    result.actualSequence == 1003 &&
                    result.advanceReason == SwRtpJitterBuffer::AdvanceReason::AgeLimit,
                scenario,
                "timed gap advance did not release the aged packet") && ok;

    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

bool runRtspUdpPunchOptionScenario() {
    const char* scenario = "RTSP UDP punch option";
    SwMediaOpenOptions defaultOptions =
        SwMediaOpenOptions::fromUrl("rtsp://127.0.0.1:5004/video?transport=udp");
    bool ok = expect(defaultOptions.udpPunch,
                     scenario,
                     "standards-compliant RTSP UDP probes should be enabled by default");

    SwMediaOpenOptions explicitOptions =
        SwMediaOpenOptions::fromUrl(
            "rtsp://127.0.0.1:5004/video?transport=udp&decoder=platform-software&udp_punch=1&token=abc");
    ok = expect(explicitOptions.udpPunch,
                scenario,
                "udp_punch=1 was not parsed") && ok;
    ok = expect(explicitOptions.sourceUrl() == "rtsp://127.0.0.1:5004/video?token=abc",
                scenario,
                "local RTSP UDP punch option leaked into source URL") && ok;
    SwMediaOpenOptions tunedOptions =
        SwMediaOpenOptions::fromUrl(
            "rtsp://127.0.0.1:5004/video?transport=udp&latency=1000&jitter=200&jitter_packets=400&token=abc");
    ok = expect(tunedOptions.sourceUrl() == "rtsp://127.0.0.1:5004/video?token=abc",
                scenario,
                "local RTP buffering options leaked into source URL") && ok;
    SwMediaOpenOptions disabledOptions =
        SwMediaOpenOptions::fromUrl("rtsp://127.0.0.1:5004/video?transport=udp&udp_punch=0");
    ok = expect(!disabledOptions.udpPunch,
                scenario,
                "udp_punch=0 should disable startup UDP probes") && ok;

    if (ok) {
        std::cout << "[" << scenario << "] PASS\n";
    }
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok = runSdpApplyScenario() && ok;
    ok = runMpegTsRtpScenario() && ok;
    ok = runFactoryRoutingScenario() && ok;
    ok = runMultiTrackSdpScenario() && ok;
    ok = runSdpFileScenario() && ok;
    ok = runRtpDescriptorScenario() && ok;
    ok = runRtpJitterBufferScenario() && ok;
    ok = runRtspUdpPunchOptionScenario() && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
