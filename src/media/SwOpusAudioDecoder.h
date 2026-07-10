#pragma once

/**
 * @file src/media/SwOpusAudioDecoder.h
 * @ingroup media
 * @brief Declares a libopus-backed Opus audio decoder for Linux.
 *
 * `libopus` is loaded with `dlopen` at runtime — no build-time or link-time dependency.
 * The tiny API subset used here (`opus_decoder_create` / `opus_decode_float` /
 * `opus_decoder_destroy`) has been ABI-stable since libopus 1.0 (2012) and is declared
 * locally, mirroring the dlopen pattern used by `SwLinuxVideoDecoder` and
 * `SwAlsaAudioSink`.
 *
 * The decoder registers itself in `SwAudioDecoderFactory` for `SwAudioPacket::Codec::Opus`
 * (ids `platform` and `libopus`). When the runtime library is missing the creators return
 * `nullptr`, so `acquire()` falls through gracefully.
 */

#include "media/SwAudioDecoder.h"

#if defined(__linux__)

#include <dlfcn.h>

#include <cstdint>
#include <vector>

static constexpr const char* kSwLogCategory_SwOpusAudioDecoder = "sw.media.swopusaudiodecoder";

/**
 * @brief Runtime loader for the subset of libopus used by SwOpusAudioDecoder.
 */
class SwOpusLibrary {
public:
    using DecoderCreateFn = void* (*)(std::int32_t sampleRate, int channels, int* error);
    using DecodeFloatFn = int (*)(void* decoder,
                                  const unsigned char* data,
                                  std::int32_t length,
                                  float* pcm,
                                  int frameSize,
                                  int decodeFec);
    using DecoderDestroyFn = void (*)(void* decoder);

    DecoderCreateFn decoderCreate{nullptr};
    DecodeFloatFn decodeFloat{nullptr};
    DecoderDestroyFn decoderDestroy{nullptr};

    static SwOpusLibrary& instance() {
        static SwOpusLibrary g_library;
        return g_library;
    }

    bool available() const { return m_available; }

private:
    SwOpusLibrary() {
        static const char* kCandidates[] = {"libopus.so.0", "libopus.so"};
        for (std::size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
            m_handle = ::dlopen(kCandidates[i], RTLD_NOW | RTLD_LOCAL);
            if (m_handle) {
                break;
            }
        }
        if (!m_handle) {
            return;
        }
        decoderCreate =
            reinterpret_cast<DecoderCreateFn>(::dlsym(m_handle, "opus_decoder_create"));
        decodeFloat = reinterpret_cast<DecodeFloatFn>(::dlsym(m_handle, "opus_decode_float"));
        decoderDestroy =
            reinterpret_cast<DecoderDestroyFn>(::dlsym(m_handle, "opus_decoder_destroy"));
        m_available = decoderCreate && decodeFloat && decoderDestroy;
    }

    void* m_handle{nullptr};
    bool m_available{false};
};

class SwOpusAudioDecoder : public SwAudioDecoder {
public:
    SwOpusAudioDecoder() = default;

    ~SwOpusAudioDecoder() override {
        destroyDecoder_();
    }

    const char* name() const override { return "SwOpusAudioDecoder"; }

    static bool runtimeAvailable() {
        return SwOpusLibrary::instance().available();
    }

    bool decode(const SwAudioPacket& packet, SwAudioFrame& frame) override {
        if (packet.codec() != SwAudioPacket::Codec::Opus || packet.payload().isEmpty()) {
            return false;
        }
        SwOpusLibrary& opus = SwOpusLibrary::instance();
        if (!opus.available()) {
            return false;
        }

        const int sampleRate = normalizedSampleRate_(
            packet.sampleRate() > 0 ? packet.sampleRate() : packet.clockRate());
        const int channels = normalizedChannelCount_(packet.channelCount());
        if (!ensureDecoder_(sampleRate, channels)) {
            return false;
        }

        // 120 ms is the maximum Opus frame duration. The scratch buffer is a member so the
        // per-packet path performs no allocation.
        const int maxSamplesPerChannel = (m_sampleRate * 120) / 1000;
        const std::size_t scratchSamples =
            static_cast<std::size_t>(maxSamplesPerChannel) * m_channels;
        if (m_pcm.size() < scratchSamples) {
            m_pcm.resize(scratchSamples);
        }
        const int decoded = opus.decodeFloat(
            m_decoder,
            reinterpret_cast<const unsigned char*>(packet.payload().constData()),
            static_cast<std::int32_t>(packet.payload().size()),
            m_pcm.data(),
            maxSamplesPerChannel,
            0);
        if (decoded <= 0) {
            swCWarning(kSwLogCategory_SwOpusAudioDecoder)
                << "[SwOpusAudioDecoder] opus_decode_float failed: " << decoded;
            return false;
        }

        frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
        frame.setSampleRate(m_sampleRate);
        frame.setChannelCount(m_channels);
        frame.setTimestamp(packet.pts());
        frame.setPayload(SwByteArray(reinterpret_cast<const char*>(m_pcm.data()),
                                     static_cast<std::size_t>(decoded) * m_channels *
                                         sizeof(float)));
        return true;
    }

    void flush() override {
        destroyDecoder_();
    }

private:
    static int normalizedSampleRate_(int requested) {
        switch (requested) {
        case 8000:
        case 12000:
        case 16000:
        case 24000:
        case 48000:
            return requested;
        default:
            return 48000;
        }
    }

    static int normalizedChannelCount_(int requested) {
        if (requested == 1) {
            return 1;
        }
        return 2;
    }

    bool ensureDecoder_(int sampleRate, int channels) {
        if (m_decoder && m_sampleRate == sampleRate && m_channels == channels) {
            return true;
        }
        destroyDecoder_();
        int error = 0;
        m_decoder = SwOpusLibrary::instance().decoderCreate(sampleRate, channels, &error);
        if (!m_decoder || error != 0) {
            swCWarning(kSwLogCategory_SwOpusAudioDecoder)
                << "[SwOpusAudioDecoder] opus_decoder_create(" << sampleRate << "Hz x"
                << channels << ") failed: " << error;
            m_decoder = nullptr;
            return false;
        }
        m_sampleRate = sampleRate;
        m_channels = channels;
        return true;
    }

    void destroyDecoder_() {
        if (m_decoder) {
            SwOpusLibrary::instance().decoderDestroy(m_decoder);
            m_decoder = nullptr;
        }
    }

    void* m_decoder{nullptr};
    int m_sampleRate{0};
    int m_channels{0};
    std::vector<float> m_pcm{};
};

inline bool swRegisterLinuxOpusAudioDecoders() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    registered = true;
    SwAudioDecoderFactory::Creator creator = []() -> std::shared_ptr<SwAudioDecoder> {
        if (!SwOpusAudioDecoder::runtimeAvailable()) {
            return nullptr;
        }
        return std::make_shared<SwOpusAudioDecoder>();
    };
    // Probe libopus once at registration so list()/contains() report the truth on
    // machines without the library (mirrors the MF entries' available=false off-platform).
    const bool runtimeAvailable = SwOpusAudioDecoder::runtimeAvailable();
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::Opus,
        "platform",
        "Platform Decoder (libopus)",
        creator,
        95,
        runtimeAvailable);
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::Opus,
        "libopus",
        "libopus",
        creator,
        90,
        runtimeAvailable);
    return true;
}

static const bool g_swLinuxOpusAudioDecodersRegistered = swRegisterLinuxOpusAudioDecoders();

#endif
