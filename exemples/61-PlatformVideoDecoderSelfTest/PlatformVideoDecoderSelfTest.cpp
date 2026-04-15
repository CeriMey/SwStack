#include "media/SwHevcBitstream.h"
#include "media/SwPlatformVideoDecoderIds.h"
#include "media/SwVideoDecoder.h"

#include <cstdlib>
#include <iostream>

namespace {

bool expect(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "[PlatformVideoDecoderSelfTest] FAIL " << label << "\n";
        return false;
    }
    return true;
}

bool runAliasRegistrationCheck() {
    bool ok = true;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformVideoDecoderId(),
                                                           false),
                "h264 platform alias registered") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformHardwareVideoDecoderId(),
                                                           false),
                "h264 platform hardware alias registered") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformSoftwareVideoDecoderId(),
                                                           false),
                "h264 platform software alias registered") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                           swPlatformVideoDecoderId(),
                                                           false),
                "h265 platform alias registered") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                           swPlatformHardwareVideoDecoderId(),
                                                           false),
                "h265 platform hardware alias registered") && ok;
    return ok;
}

bool runAvailabilityCheck() {
#if defined(_WIN32)
    const bool expectedH264Platform = true;
    const bool expectedH264Hardware = true;
    const bool expectedH264Software = true;
    const bool expectedH265Platform = true;
    const bool expectedH265Hardware = true;
#elif defined(__linux__)
    const bool expectedH264Platform = swLinuxH264DecoderRuntimeAvailable();
    const bool expectedH264Hardware = false;
    const bool expectedH264Software = swLinuxOpenH264RuntimeAvailable();
    const bool expectedH265Platform = swLinuxH265DecoderRuntimeAvailable();
    const bool expectedH265Hardware = swLinuxVaapiH265RuntimeAvailable();
#else
    const bool expectedH264Platform = false;
    const bool expectedH264Hardware = false;
    const bool expectedH264Software = false;
    const bool expectedH265Platform = false;
    const bool expectedH265Hardware = false;
#endif

    bool ok = true;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformVideoDecoderId()) == expectedH264Platform,
                "h264 platform availability") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformHardwareVideoDecoderId()) == expectedH264Hardware,
                "h264 platform hardware availability") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                           swPlatformSoftwareVideoDecoderId()) == expectedH264Software,
                "h264 platform software availability") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                           swPlatformVideoDecoderId()) == expectedH265Platform,
                "h265 platform availability") && ok;
    ok = expect(SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                           swPlatformHardwareVideoDecoderId()) == expectedH265Hardware,
                "h265 platform hardware availability") && ok;
    return ok;
}

bool runCreationCheck() {
    bool ok = true;
    if (SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformVideoDecoderId())) {
        auto decoder = SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H264,
                                                                swPlatformVideoDecoderId());
        ok = expect(static_cast<bool>(decoder),
                    "h264 platform create") && ok;
        if (decoder) {
            ok = expect(decoder->open(SwVideoFormatInfo()), "h264 platform open") && ok;
        }
    }
    if (SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H264,
                                                   swPlatformSoftwareVideoDecoderId())) {
        auto decoder = SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H264,
                                                                swPlatformSoftwareVideoDecoderId());
        ok = expect(static_cast<bool>(decoder),
                    "h264 platform software create") && ok;
        if (decoder) {
            ok = expect(decoder->open(SwVideoFormatInfo()), "h264 platform software open") && ok;
        }
    }
    if (SwVideoDecoderFactory::instance().contains(SwVideoPacket::Codec::H265,
                                                   swPlatformVideoDecoderId())) {
        auto decoder = SwVideoDecoderFactory::instance().create(SwVideoPacket::Codec::H265,
                                                                swPlatformVideoDecoderId());
        ok = expect(static_cast<bool>(decoder),
                    "h265 platform create") && ok;
        if (decoder) {
            ok = expect(decoder->open(SwVideoFormatInfo()), "h265 platform open") && ok;
        }
    }
    return ok;
}

bool runHevcHelperCheck() {
    SwByteArray payload;
    static const char kStartCode[] = {0x00, 0x00, 0x00, 0x01};
    payload.append(kStartCode, sizeof(kStartCode));
    payload.append(static_cast<char>(0x40));
    payload.append(static_cast<char>(0x01));
    payload.append(static_cast<char>(0xAA));
    payload.append(kStartCode, sizeof(kStartCode));
    payload.append(static_cast<char>(0x42));
    payload.append(static_cast<char>(0x01));
    payload.append(static_cast<char>(0xBB));
    payload.append(kStartCode, sizeof(kStartCode));
    payload.append(static_cast<char>(0x44));
    payload.append(static_cast<char>(0x01));
    payload.append(static_cast<char>(0xCC));

    const SwHevcSequenceHeaderInfo info = swCollectHevcSequenceHeaderInfo(payload);
    bool ok = true;
    ok = expect(info.hasVps, "hevc helper vps") && ok;
    ok = expect(info.hasSps, "hevc helper sps") && ok;
    ok = expect(info.hasPps, "hevc helper pps") && ok;
    ok = expect(info.isComplete(), "hevc helper complete") && ok;
    ok = expect(!info.annexB.isEmpty(), "hevc helper annexb") && ok;
    const SwHevcAccessUnitInfo au = swParseHevcAccessUnit(payload);
    ok = expect(au.hasParameterSets(), "hevc au parameter sets") && ok;
    ok = expect(!au.hasSlice, "hevc au no slice") && ok;
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok = runHevcHelperCheck() && ok;
    ok = runAliasRegistrationCheck() && ok;
    ok = runAvailabilityCheck() && ok;
    ok = runCreationCheck() && ok;
    if (ok) {
        std::cout << "[PlatformVideoDecoderSelfTest] PASS\n";
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
