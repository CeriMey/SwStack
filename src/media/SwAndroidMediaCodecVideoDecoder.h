#pragma once

/**
 * @file src/media/SwAndroidMediaCodecVideoDecoder.h
 * @ingroup media
 * @brief Android MediaCodec video decoder backend exposed through the SwVideoDecoder API.
 */

#include "SwDebug.h"
#include "media/SwPlatformVideoDecoderIds.h"
#include "media/SwVideoDecoder.h"
#include "media/SwVideoFrame.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

static constexpr const char* kSwLogCategory_SwAndroidMediaCodecVideoDecoder =
    "sw.media.swandroidmediacodecvideodecoder";

#if defined(__ANDROID__)

#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

class SwAndroidMediaCodecVideoDecoder : public SwVideoDecoder {
public:
    explicit SwAndroidMediaCodecVideoDecoder(SwVideoPacket::Codec codec = SwVideoPacket::Codec::Unknown,
                                             const char* decoderName = "SwAndroidMediaCodecVideoDecoder")
        : m_codec(codec),
          m_name(decoderName) {}

    ~SwAndroidMediaCodecVideoDecoder() override {
        shutdownCodec_();
    }

    const char* name() const override { return m_name; }

    bool open(const SwVideoFormatInfo& expectedFormat) override {
        if (expectedFormat.width > 0) {
            m_configuredWidth = expectedFormat.width;
        }
        if (expectedFormat.height > 0) {
            m_configuredHeight = expectedFormat.height;
        }
        return ensureCodec_();
    }

    bool feed(const SwVideoPacket& packet) override {
        if (packet.payload().isEmpty()) {
            return true;
        }
        if (m_codec == SwVideoPacket::Codec::Unknown) {
            m_codec = packet.codec();
        }
        if (packet.codec() != m_codec) {
            shutdownCodec_();
            m_codec = packet.codec();
        }
        if (!ensureCodec_()) {
            return false;
        }

        const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(m_codecHandle, 8000);
        if (inputIndex < 0) {
            drainOutput_(0);
            return inputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER;
        }

        size_t inputSize = 0;
        uint8_t* input = AMediaCodec_getInputBuffer(m_codecHandle,
                                                    static_cast<size_t>(inputIndex),
                                                    &inputSize);
        if (!input || inputSize < static_cast<size_t>(packet.payload().size())) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] input buffer too small size=" << inputSize
                << " packet=" << packet.payload().size();
            return false;
        }

        std::memcpy(input,
                    packet.payload().constData(),
                    static_cast<size_t>(packet.payload().size()));
        const uint64_t presentationUs = packetTimestampUs_(packet);
        const media_status_t queued = AMediaCodec_queueInputBuffer(
            m_codecHandle,
            static_cast<size_t>(inputIndex),
            0,
            static_cast<size_t>(packet.payload().size()),
            presentationUs,
            0);
        if (queued != AMEDIA_OK) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] queueInputBuffer failed status=" << static_cast<int>(queued);
            return false;
        }

        drainOutput_(0);
        return true;
    }

    void flush() override {
        if (m_codecHandle && m_started) {
            (void)AMediaCodec_flush(m_codecHandle);
        }
    }

    void setOutputTarget(const SwVideoOutputTarget& target) override {
        m_outputTarget = target;
        shutdownCodec_();
    }

private:
    static const char* mimeForCodec_(SwVideoPacket::Codec codec) {
        switch (codec) {
        case SwVideoPacket::Codec::H264:
            return "video/avc";
        case SwVideoPacket::Codec::H265:
            return "video/hevc";
        default:
            return nullptr;
        }
    }

    static uint64_t packetTimestampUs_(const SwVideoPacket& packet) {
        const std::int64_t pts = packet.pts();
        if (pts <= 0) {
            return 0;
        }
        const int clockRate = packet.clockRate();
        if (clockRate > 0) {
            return static_cast<uint64_t>((pts * 1000000LL) / clockRate);
        }
        return static_cast<uint64_t>(pts);
    }

    bool ensureCodec_() {
        if (m_started && m_codecHandle) {
            return true;
        }
        const char* mime = mimeForCodec_(m_codec);
        if (!mime) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] unsupported codec=" << static_cast<int>(m_codec);
            return false;
        }
        if (!m_outputTarget.isValid() ||
            m_outputTarget.kind() != SwVideoOutputTarget::Kind::AndroidNativeWindow) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] Android output surface is not configured";
            return false;
        }

        ANativeWindow* window = static_cast<ANativeWindow*>(m_outputTarget.handle());
        if (!window) {
            return false;
        }

        m_codecHandle = AMediaCodec_createDecoderByType(mime);
        if (!m_codecHandle) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] AMediaCodec_createDecoderByType failed mime=" << mime;
            return false;
        }

        AMediaFormat* format = AMediaFormat_new();
        if (!format) {
            shutdownCodec_();
            return false;
        }
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, std::max(1, m_configuredWidth));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, std::max(1, m_configuredHeight));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 4 * 1024 * 1024);

        const media_status_t configured =
            AMediaCodec_configure(m_codecHandle, format, window, nullptr, 0);
        AMediaFormat_delete(format);
        if (configured != AMEDIA_OK) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] configure failed status=" << static_cast<int>(configured);
            shutdownCodec_();
            return false;
        }

        const media_status_t started = AMediaCodec_start(m_codecHandle);
        if (started != AMEDIA_OK) {
            swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
                << "[" << m_name << "] start failed status=" << static_cast<int>(started);
            shutdownCodec_();
            return false;
        }
        m_started = true;
        swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
            << "[" << m_name << "] configured mime=" << mime
            << " surface=" << m_outputTarget.handle();
        return true;
    }

    void drainOutput_(int64_t timeoutUs) {
        if (!m_codecHandle || !m_started) {
            return;
        }

        for (;;) {
            AMediaCodecBufferInfo info{};
            const ssize_t outputIndex =
                AMediaCodec_dequeueOutputBuffer(m_codecHandle, &info, timeoutUs);
            if (outputIndex >= 0) {
                const bool render =
                    (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) == 0 &&
                    info.size > 0 &&
                    m_outputTarget.isValid();
                AMediaCodec_releaseOutputBuffer(m_codecHandle,
                                                static_cast<size_t>(outputIndex),
                                                render);
                if (render) {
                    emitPresentedFrame_(info.presentationTimeUs);
                }
                timeoutUs = 0;
                continue;
            }
            if (outputIndex == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                updateOutputFormat_();
                timeoutUs = 0;
                continue;
            }
            if (outputIndex == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED ||
                outputIndex == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                return;
            }
            return;
        }
    }

    void updateOutputFormat_() {
        AMediaFormat* format = AMediaCodec_getOutputFormat(m_codecHandle);
        if (!format) {
            return;
        }
        int32_t width = 0;
        int32_t height = 0;
        if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width) && width > 0) {
            m_outputWidth = width;
        }
        if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height) && height > 0) {
            m_outputHeight = height;
        }
        AMediaFormat_delete(format);
        swCWarning(kSwLogCategory_SwAndroidMediaCodecVideoDecoder)
            << "[" << m_name << "] output format "
            << m_outputWidth << "x" << m_outputHeight;
    }

    void emitPresentedFrame_(int64_t timestampUs) {
        const int width = std::max(1, m_outputWidth);
        const int height = std::max(1, m_outputHeight);
        SwVideoFormatInfo info = SwDescribeVideoFormat(SwVideoPixelFormat::NV12, width, height);
        SwVideoFrame frame = SwVideoFrame::wrapNativeAndroidSurfacePresented(info,
                                                                             m_outputTarget.owner());
        frame.setTimestamp(timestampUs);
        emitFrame(frame);
    }

    void shutdownCodec_() {
        if (m_codecHandle) {
            if (m_started) {
                (void)AMediaCodec_stop(m_codecHandle);
            }
            AMediaCodec_delete(m_codecHandle);
            m_codecHandle = nullptr;
        }
        m_started = false;
    }

    SwVideoPacket::Codec m_codec{SwVideoPacket::Codec::Unknown};
    const char* m_name{nullptr};
    SwVideoOutputTarget m_outputTarget{};
    AMediaCodec* m_codecHandle{nullptr};
    bool m_started{false};
    int m_configuredWidth{1920};
    int m_configuredHeight{1080};
    int m_outputWidth{1920};
    int m_outputHeight{1080};
};

inline bool swRegisterAndroidVideoDecoders() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    registered = true;

    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        swPlatformVideoDecoderId(),
        "Android MediaCodec",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H264,
                "SwAndroidMediaCodecH264Decoder");
        },
        100,
        false,
        true);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        swPlatformHardwareVideoDecoderId(),
        "Android MediaCodec Hardware",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H264,
                "SwAndroidMediaCodecH264DecoderHW");
        },
        90,
        false,
        true);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        "android-mediacodec",
        "Android MediaCodec",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H264,
                "SwAndroidMediaCodecH264Decoder");
        },
        80,
        false,
        true);

    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        swPlatformVideoDecoderId(),
        "Android MediaCodec",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H265,
                "SwAndroidMediaCodecH265Decoder");
        },
        100,
        false,
        true);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        swPlatformHardwareVideoDecoderId(),
        "Android MediaCodec Hardware",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H265,
                "SwAndroidMediaCodecH265DecoderHW");
        },
        90,
        false,
        true);
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        "android-mediacodec",
        "Android MediaCodec",
        []() {
            return std::make_shared<SwAndroidMediaCodecVideoDecoder>(
                SwVideoPacket::Codec::H265,
                "SwAndroidMediaCodecH265Decoder");
        },
        80,
        false,
        true);

    return true;
}

static const bool g_swAndroidVideoDecodersRegistered = swRegisterAndroidVideoDecoders();

#endif
