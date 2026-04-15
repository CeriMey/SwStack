#pragma once

/**
 * @file src/media/SwMediaFoundationAudioDecoder.h
 * @ingroup media
 * @brief Declares the Media Foundation audio decoder backends exposed through SwAudioDecoder.
 */

#include "SwDebug.h"
#include "media/SwAudioDecoder.h"

#if defined(_WIN32)
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <combaseapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>

static constexpr const char* kSwLogCategory_SwMediaFoundationAudioDecoder =
    "sw.media.swmediafoundationaudiodecoder";

class SwMediaFoundationAudioDecoderBase : public SwAudioDecoder {
public:
    SwMediaFoundationAudioDecoderBase(SwAudioPacket::Codec codec, const char* decoderName)
        : m_targetCodec(codec), m_name(decoderName) {}

    ~SwMediaFoundationAudioDecoderBase() override {
        shutdown_();
    }

    const char* name() const override { return m_name; }

    bool decode(const SwAudioPacket& packet, SwAudioFrame& frame) override {
        if (packet.codec() != m_targetCodec || packet.payload().isEmpty()) {
            return false;
        }

        const int sampleRate = packet.sampleRate() > 0 ? packet.sampleRate() : defaultSampleRate_();
        const int channelCount = packet.channelCount() > 0 ? packet.channelCount() : defaultChannelCount_();
        if (sampleRate <= 0 || channelCount <= 0) {
            return false;
        }

        if (m_ready && (sampleRate != m_inputSampleRate || channelCount != m_inputChannelCount)) {
            shutdown_();
        }

        m_inputSampleRate = sampleRate;
        m_inputChannelCount = channelCount;

        if (!ensureInitialized_(packet)) {
            if (!m_loggedInitializationFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] Initialization failed before decode";
            }
            return false;
        }
        m_loggedInitializationFailure.store(false);

        if (!pushInput_(packet)) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] pushInput failed bytes=" << packet.payload().size()
                << " codec=" << static_cast<int>(packet.codec());
            return false;
        }

        drainOutput_();
        if (m_pendingFrames.empty()) {
            return false;
        }

        frame = std::move(m_pendingFrames.front());
        m_pendingFrames.pop_front();
        return frame.isValid();
    }

    void flush() override {
        m_pendingFrames.clear();
        if (m_decoder) {
            m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        }
        m_nextInputDiscontinuity.store(true);
    }

protected:
    virtual GUID inputSubtype() const = 0;
    virtual HRESULT createDecoder(IMFTransform** decoder) const = 0;
    virtual int defaultSampleRate_() const { return 48000; }
    virtual int defaultChannelCount_() const { return 2; }

    virtual void configureInputType_(IMFMediaType* type, const SwAudioPacket& packet) const {
        if (!type) {
            return;
        }
        const UINT32 sampleRate = static_cast<UINT32>(packet.sampleRate() > 0 ? packet.sampleRate()
                                                                               : defaultSampleRate_());
        const UINT32 channels = static_cast<UINT32>(packet.channelCount() > 0 ? packet.channelCount()
                                                                              : defaultChannelCount_());
        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
        type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * channels);
        type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    }

    HRESULT createDecoderFromEnum_(const GUID& subtype, IMFTransform** decoder) const {
        if (!decoder) {
            return E_POINTER;
        }
        *decoder = nullptr;
        MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Audio, subtype};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
                                   MFT_ENUM_FLAG_SORTANDFILTER,
                               &inputInfo,
                               nullptr,
                               &activates,
                               &count);
        if (FAILED(hr)) {
            return hr;
        }
        if (count == 0 || !activates) {
            if (activates) {
                CoTaskMemFree(activates);
            }
            return MF_E_TOPO_CODEC_NOT_FOUND;
        }
        hr = MF_E_TOPO_CODEC_NOT_FOUND;
        for (UINT32 i = 0; i < count; ++i) {
            hr = activates[i]->ActivateObject(IID_PPV_ARGS(decoder));
            if (SUCCEEDED(hr) && *decoder) {
                break;
            }
        }
        for (UINT32 i = 0; i < count; ++i) {
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
        return hr;
    }

private:
    bool ensureInitialized_(const SwAudioPacket& packet) {
        if (m_ready) {
            return true;
        }
        if (!m_comInit_) {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] CoInitializeEx failed hr=0x" << std::hex << hr << std::dec;
                return false;
            }
            m_comInit_ = (hr != RPC_E_CHANGED_MODE);
        }
        if (!m_mfStarted_) {
            HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
            if (FAILED(hr)) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] MFStartup failed hr=0x" << std::hex << hr << std::dec;
                return false;
            }
            m_mfStarted_ = true;
        }

        Microsoft::WRL::ComPtr<IMFTransform> decoder;
        HRESULT hr = createDecoder(&decoder);
        if (FAILED(hr) || !decoder) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] Failed to create decoder hr=0x" << std::hex << hr << std::dec;
            return false;
        }
        m_decoder = decoder;
        configureLowLatencyMode_();
        if (!setInputType_(packet)) {
            return false;
        }
        setOutputType_(false);
        maybeStartStreaming_();
        m_nextInputDiscontinuity.store(true);
        m_ready = true;
        return true;
    }

    bool setInputType_(const SwAudioPacket& packet) {
        Microsoft::WRL::ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) {
            return false;
        }
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, inputSubtype());
        configureInputType_(type.Get(), packet);

        hr = m_decoder->SetInputType(0, type.Get(), 0);
        if (FAILED(hr)) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] SetInputType failed hr=0x" << std::hex << hr << std::dec
                << " rate=" << m_inputSampleRate << " channels=" << m_inputChannelCount;
            return false;
        }
        swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
            << "[" << m_name << "] Input type configured subtype="
            << subtypeName_(inputSubtype())
            << " rate=" << m_inputSampleRate
            << " channels=" << m_inputChannelCount;
        return true;
    }

    static int outputSubtypeRank_(const GUID& subtype) {
        if (subtype == MFAudioFormat_Float) {
            return 0;
        }
        if (subtype == MFAudioFormat_PCM) {
            return 1;
        }
        return -1;
    }

    static const char* subtypeName_(const GUID& subtype) {
        if (subtype == MFAudioFormat_Opus) {
            return "Opus";
        }
        if (subtype == MFAudioFormat_AAC) {
            return "AAC";
        }
        if (subtype == MFAudioFormat_Float) {
            return "Float";
        }
        if (subtype == MFAudioFormat_PCM) {
            return "PCM";
        }
        return "other";
    }

    bool setOutputType_(bool logOnFailure = true) {
        if (!m_decoder) {
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> bestType;
        GUID bestSubtype = GUID_NULL;
        int bestRank = (std::numeric_limits<int>::max)();
        for (UINT32 i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IMFMediaType> type;
            HRESULT hr = m_decoder->GetOutputAvailableType(0, i, &type);
            if (hr == MF_E_NO_MORE_TYPES) {
                break;
            }
            if (FAILED(hr) || !type) {
                break;
            }
            GUID subtype = GUID_NULL;
            if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
                continue;
            }
            const int rank = outputSubtypeRank_(subtype);
            if (rank < 0 || rank >= bestRank) {
                continue;
            }
            bestRank = rank;
            bestSubtype = subtype;
            bestType = type;
        }

        if (!bestType) {
            if (logOnFailure && !m_loggedOutputTypeFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] No supported output type available yet";
            }
            return false;
        }

        HRESULT hr = m_decoder->SetOutputType(0, bestType.Get(), 0);
        if (FAILED(hr)) {
            if (logOnFailure && !m_loggedOutputTypeFailure.exchange(true)) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] Failed to negotiate output type subtype="
                    << subtypeName_(bestSubtype)
                    << " hr=0x" << std::hex << hr << std::dec;
            }
            return false;
        }

        UINT32 sampleRate = 0;
        UINT32 channels = 0;
        UINT32 bitsPerSample = 0;
        bestType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        bestType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        bestType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
        m_outputSubtype = bestSubtype;
        m_outputSampleRate = static_cast<int>(sampleRate);
        m_outputChannelCount = static_cast<int>(channels);
        m_outputBitsPerSample = static_cast<int>(bitsPerSample);
        m_outputTypeReady = true;
        m_loggedOutputTypeFailure.store(false);
        swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
            << "[" << m_name << "] Selected output type "
            << subtypeName_(bestSubtype)
            << " rate=" << m_outputSampleRate
            << " channels=" << m_outputChannelCount
            << " bits=" << m_outputBitsPerSample;
        return true;
    }

    void maybeStartStreaming_() {
        if (!m_decoder || m_streamingStarted_) {
            return;
        }
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        m_streamingStarted_ = true;
    }

    void configureLowLatencyMode_() {
        if (!m_decoder) {
            return;
        }
        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        if (SUCCEEDED(m_decoder->GetAttributes(&attributes)) && attributes) {
            attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
        }
    }

    bool pushInput_(const SwAudioPacket& packet) {
        Microsoft::WRL::ComPtr<IMFSample> sample;
        HRESULT hr = MFCreateSample(&sample);
        if (FAILED(hr)) {
            return false;
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.payload().size()), &buffer);
        if (FAILED(hr)) {
            return false;
        }

        BYTE* dst = nullptr;
        DWORD maxLen = 0;
        hr = buffer->Lock(&dst, &maxLen, nullptr);
        if (FAILED(hr)) {
            return false;
        }
        const size_t toCopy = std::min(static_cast<size_t>(maxLen),
                                       static_cast<size_t>(packet.payload().size()));
        std::memcpy(dst, packet.payload().constData(), toCopy);
        buffer->Unlock();
        buffer->SetCurrentLength(static_cast<DWORD>(toCopy));
        sample->AddBuffer(buffer.Get());

        if (packet.pts() >= 0 && m_inputSampleRate > 0) {
            const LONGLONG hns =
                static_cast<LONGLONG>((packet.pts() * 10000000LL) / m_inputSampleRate);
            sample->SetSampleTime(hns);
        }
        if (packet.isDiscontinuity() || m_nextInputDiscontinuity.exchange(false)) {
            sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        }

        hr = m_decoder->ProcessInput(0, sample.Get(), 0);
        if (FAILED(hr) && hr == MF_E_NOTACCEPTING) {
            drainOutput_();
            hr = m_decoder->ProcessInput(0, sample.Get(), 0);
        }
        if (FAILED(hr)) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] ProcessInput failed hr=0x" << std::hex << hr << std::dec;
        }
        if (SUCCEEDED(hr) && !m_outputTypeReady) {
            setOutputType_(false);
        }
        return SUCCEEDED(hr);
    }

    void drainOutput_() {
        if (!m_decoder) {
            return;
        }
        if (!m_outputTypeReady && !setOutputType_(false)) {
            return;
        }
        while (true) {
            MFT_OUTPUT_STREAM_INFO info{};
            HRESULT hr = m_decoder->GetOutputStreamInfo(0, &info);
            if (FAILED(hr)) {
                return;
            }

            Microsoft::WRL::ComPtr<IMFSample> sample;
            if ((info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
                hr = MFCreateSample(&sample);
                if (FAILED(hr)) {
                    return;
                }
                Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
                hr = MFCreateMemoryBuffer(info.cbSize > 0 ? info.cbSize : 65536, &buffer);
                if (FAILED(hr)) {
                    return;
                }
                sample->AddBuffer(buffer.Get());
            }

            MFT_OUTPUT_DATA_BUFFER output{};
            output.pSample = sample.Get();
            DWORD status = 0;
            hr = m_decoder->ProcessOutput(0, 1, &output, &status);
            if (output.pEvents) {
                output.pEvents->Release();
                output.pEvents = nullptr;
            }
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                return;
            }
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                m_outputTypeReady = false;
                m_outputSubtype = GUID_NULL;
                if (!setOutputType_()) {
                    return;
                }
                continue;
            }
            if (hr == MF_E_TRANSFORM_TYPE_NOT_SET) {
                m_outputTypeReady = false;
                m_outputSubtype = GUID_NULL;
                if (!setOutputType_(false)) {
                    return;
                }
                continue;
            }
            if (FAILED(hr)) {
                swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                    << "[" << m_name << "] ProcessOutput failed hr=0x" << std::hex << hr << std::dec;
                return;
            }

            IMFSample* outSample = output.pSample;
            if (!outSample) {
                outSample = sample.Get();
            }
            appendFrameFromSample_(outSample);
            if (output.pSample && output.pSample != sample.Get()) {
                output.pSample->Release();
            }
        }
    }

    void appendFrameFromSample_(IMFSample* sample) {
        if (!sample) {
            return;
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) {
            return;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        if (FAILED(buffer->Lock(&data, &maxLength, &currentLength)) || !data || currentLength == 0) {
            return;
        }

        SwAudioFrame frame;
        frame.setSampleFormat(SwAudioFrame::SampleFormat::Float32);
        frame.setSampleRate(m_outputSampleRate > 0 ? m_outputSampleRate : m_inputSampleRate);
        frame.setChannelCount(m_outputChannelCount > 0 ? m_outputChannelCount : m_inputChannelCount);

        SwByteArray payload;
        if (m_outputSubtype == MFAudioFormat_Float) {
            payload = SwByteArray(reinterpret_cast<const char*>(data), currentLength);
        } else if (m_outputSubtype == MFAudioFormat_PCM && m_outputBitsPerSample == 16) {
            const std::size_t sampleCount = static_cast<std::size_t>(currentLength) / sizeof(int16_t);
            std::vector<float> samples(sampleCount);
            const int16_t* src = reinterpret_cast<const int16_t*>(data);
            for (std::size_t i = 0; i < sampleCount; ++i) {
                samples[i] = static_cast<float>(src[i]) / 32768.0f;
            }
            payload = SwByteArray(reinterpret_cast<const char*>(samples.data()),
                                  static_cast<int>(samples.size() * sizeof(float)));
        } else if (m_outputSubtype == MFAudioFormat_PCM && m_outputBitsPerSample == 32) {
            const std::size_t sampleCount = static_cast<std::size_t>(currentLength) / sizeof(int32_t);
            std::vector<float> samples(sampleCount);
            const int32_t* src = reinterpret_cast<const int32_t*>(data);
            for (std::size_t i = 0; i < sampleCount; ++i) {
                samples[i] = static_cast<float>(src[i] / 2147483648.0);
            }
            payload = SwByteArray(reinterpret_cast<const char*>(samples.data()),
                                  static_cast<int>(samples.size() * sizeof(float)));
        }
        buffer->Unlock();

        if (payload.isEmpty()) {
            return;
        }

        LONGLONG sampleTime = 0;
        if (SUCCEEDED(sample->GetSampleTime(&sampleTime)) && m_inputSampleRate > 0) {
            frame.setTimestamp(static_cast<std::int64_t>(
                (sampleTime * static_cast<LONGLONG>(m_inputSampleRate)) / 10000000LL));
        }
        frame.setPayload(std::move(payload));
        if (!frame.isValid()) {
            return;
        }

        auto decoded = ++m_decodedFrameCount;
        if (!m_loggedFirstFrame.exchange(true)) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] First decoded audio frame rate=" << frame.sampleRate()
                << " channels=" << frame.channelCount()
                << " samples=" << frame.sampleCount();
        } else if ((decoded % 50) == 0) {
            swCWarning(kSwLogCategory_SwMediaFoundationAudioDecoder)
                << "[" << m_name << "] Decoded audio frame count=" << decoded
                << " ts=" << frame.timestamp();
        }
        m_pendingFrames.push_back(std::move(frame));
    }

    void shutdown_() {
        m_pendingFrames.clear();
        m_decoder.Reset();
        if (m_mfStarted_) {
            MFShutdown();
            m_mfStarted_ = false;
        }
        if (m_comInit_) {
            CoUninitialize();
            m_comInit_ = false;
        }
        m_ready = false;
        m_outputTypeReady = false;
        m_streamingStarted_ = false;
        m_outputSubtype = GUID_NULL;
        m_outputSampleRate = 0;
        m_outputChannelCount = 0;
        m_outputBitsPerSample = 0;
        m_loggedFirstFrame.store(false);
    }

    SwAudioPacket::Codec m_targetCodec{SwAudioPacket::Codec::Unknown};
    const char* m_name{nullptr};
    bool m_comInit_{false};
    bool m_mfStarted_{false};
    bool m_ready{false};
    bool m_outputTypeReady{false};
    bool m_streamingStarted_{false};
    int m_inputSampleRate{0};
    int m_inputChannelCount{0};
    int m_outputSampleRate{0};
    int m_outputChannelCount{0};
    int m_outputBitsPerSample{0};
    GUID m_outputSubtype{GUID_NULL};
    Microsoft::WRL::ComPtr<IMFTransform> m_decoder;
    std::deque<SwAudioFrame> m_pendingFrames{};
    std::atomic<bool> m_nextInputDiscontinuity{true};
    std::atomic<bool> m_loggedInitializationFailure{false};
    std::atomic<bool> m_loggedOutputTypeFailure{false};
    std::atomic<bool> m_loggedFirstFrame{false};
    std::atomic<uint64_t> m_decodedFrameCount{0};
};

class SwMediaFoundationOpusDecoder : public SwMediaFoundationAudioDecoderBase {
public:
    SwMediaFoundationOpusDecoder()
        : SwMediaFoundationAudioDecoderBase(SwAudioPacket::Codec::Opus,
                                            "SwMediaFoundationOpusDecoder") {}

    static bool isSystemDecoderAvailable() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            return false;
        }
        bool mfStarted = false;
        hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) {
            if (comInitialized) {
                CoUninitialize();
            }
            return false;
        }
        mfStarted = true;

        bool available = false;
        Microsoft::WRL::ComPtr<IMFTransform> decoder;
        hr = CoCreateInstance(CLSID_MSOpusDecoder,
                              nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(decoder.GetAddressOf()));
        if (FAILED(hr) || !decoder) {
            MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Audio, MFAudioFormat_Opus};
            IMFActivate** activates = nullptr;
            UINT32 count = 0;
            hr = MFTEnumEx(MFT_CATEGORY_AUDIO_DECODER,
                           MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
                               MFT_ENUM_FLAG_SORTANDFILTER,
                           &inputInfo,
                           nullptr,
                           &activates,
                           &count);
            if (SUCCEEDED(hr) && activates && count > 0) {
                hr = activates[0]->ActivateObject(IID_PPV_ARGS(decoder.GetAddressOf()));
                for (UINT32 i = 0; i < count; ++i) {
                    activates[i]->Release();
                }
                CoTaskMemFree(activates);
            }
        }
        available = SUCCEEDED(hr) && decoder;

        if (mfStarted) {
            MFShutdown();
        }
        if (comInitialized) {
            CoUninitialize();
        }
        return available;
    }

protected:
    GUID inputSubtype() const override { return MFAudioFormat_Opus; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        if (!decoder) {
            return E_POINTER;
        }
        *decoder = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_MSOpusDecoder,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(decoder));
        if (SUCCEEDED(hr) && *decoder) {
            return hr;
        }
        return createDecoderFromEnum_(MFAudioFormat_Opus, decoder);
    }

    void configureInputType_(IMFMediaType* type, const SwAudioPacket& packet) const override {
        SwMediaFoundationAudioDecoderBase::configureInputType_(type, packet);
        if (!type) {
            return;
        }
        const UINT32 sampleRate = static_cast<UINT32>(packet.sampleRate() > 0 ? packet.sampleRate() : 48000);
        const UINT32 channels = static_cast<UINT32>(packet.channelCount() > 0 ? packet.channelCount() : 2);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * channels * 2);
    }
};

inline bool swRegisterMediaFoundationAudioDecoders() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    registered = true;
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::Opus,
        "platform",
        "Platform Decoder",
        []() -> std::shared_ptr<SwAudioDecoder> {
            return std::make_shared<SwMediaFoundationOpusDecoder>();
        },
        100,
#if defined(_WIN32)
        true);
#else
        false);
#endif
    SwAudioDecoderFactory::instance().registerDecoder(
        SwAudioPacket::Codec::Opus,
        "media-foundation",
        "Media Foundation",
        []() -> std::shared_ptr<SwAudioDecoder> {
            return std::make_shared<SwMediaFoundationOpusDecoder>();
        },
        90,
#if defined(_WIN32)
        true);
#else
        false);
#endif
    return true;
}

static const bool g_swMediaFoundationAudioDecodersRegistered =
    swRegisterMediaFoundationAudioDecoders();

#endif
