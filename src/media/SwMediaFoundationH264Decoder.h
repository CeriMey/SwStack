/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once

/***************************************************************************************************
 * Windows-only H.26x decoder(s) using Media Foundation MFTs.
 * Converts to BGRA32 SwVideoFrames and emits via SwVideoDecoder interface.
 **************************************************************************************************/

#include "media/SwVideoDecoder.h"
#include "media/SwVideoPacket.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <memory>
static constexpr const char* kSwLogCategory_SwMediaFoundationH264Decoder = "sw.media.swmediafoundationh264decoder";


#if defined(_WIN32)
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>
#include <combaseapi.h>
#include <wmcodecdsp.h>



class SwMediaFoundationDecoderBase : public SwVideoDecoder {
public:
    SwMediaFoundationDecoderBase(SwVideoPacket::Codec codec, const char* decoderName)
        : m_targetCodec(codec), m_name(decoderName) {}
    ~SwMediaFoundationDecoderBase() override {
        shutdown();
    }

    const char* name() const override { return m_name; }

    bool open(const SwVideoFormatInfo& expectedFormat) override {
        (void)expectedFormat;
        return ensureInitialized();
    }

    bool feed(const SwVideoPacket& packet) override {
        if (packet.codec() != m_targetCodec || packet.payload().isEmpty()) {
            return false;
        }
        if (!ensureInitialized()) {
            return false;
        }
        if (!pushInput(packet)) {
            return false;
        }
        drainOutput();
        return true;
    }

    void flush() override {
        if (m_decoder) {
            m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        }
    }

protected:
    virtual GUID inputSubtype() const = 0;
    virtual HRESULT createDecoder(IMFTransform** decoder) const = 0;
    virtual void configureInputType(IMFMediaType* type) const {
        (void)type;
    }

    HRESULT createDecoderFromEnum(const GUID& subtype, IMFTransform** decoder) const {
        if (!decoder) {
            return E_POINTER;
        }
        MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video, subtype};
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                               MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_HARDWARE,
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
        hr = activates[0]->ActivateObject(IID_PPV_ARGS(decoder));
        for (UINT32 i = 0; i < count; ++i) {
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
        return hr;
    }

private:
    bool ensureInitialized() {
        if (m_ready) {
            return true;
        }
        if (!m_comInit && FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
            return false;
        }
        m_comInit = true;
        if (!m_mfStarted && FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            return false;
        }
        m_mfStarted = true;

        Microsoft::WRL::ComPtr<IMFTransform> decoder;
        HRESULT hr = createDecoder(&decoder);
        if (FAILED(hr)) {
            swCError(kSwLogCategory_SwMediaFoundationH264Decoder) << "[" << m_name << "] Failed to create decoder: 0x" << std::hex << hr << std::dec;
            return false;
        }
        m_decoder = decoder;

        if (!setInputType()) {
            return false;
        }
        if (!setOutputType()) {
            return false;
        }

        m_decoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        m_decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
        m_ready = true;
        return true;
    }

    bool setInputType() {
        Microsoft::WRL::ComPtr<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(&type);
        if (FAILED(hr)) {
            return false;
        }
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, inputSubtype());
        type->SetUINT32(MF_MT_AVG_BITRATE, 8000000);
        configureInputType(type.Get());
        hr = m_decoder->SetInputType(0, type.Get(), 0);
        return SUCCEEDED(hr);
    }

    bool setOutputType() {
        const GUID preferred[] = {MFVideoFormat_NV12, MFVideoFormat_RGB32};
        for (const auto& guid : preferred) {
            for (UINT32 i = 0;; ++i) {
                Microsoft::WRL::ComPtr<IMFMediaType> type;
                HRESULT hr = m_decoder->GetOutputAvailableType(0, i, &type);
                if (hr == MF_E_NO_MORE_TYPES) {
                    break;
                }
                if (FAILED(hr)) {
                    break;
                }
                GUID subtype{};
                type->GetGUID(MF_MT_SUBTYPE, &subtype);
                if (subtype != guid) {
                    continue;
                }
                hr = m_decoder->SetOutputType(0, type.Get(), 0);
                if (SUCCEEDED(hr)) {
                    UINT32 width = 0, height = 0;
                    if (SUCCEEDED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height))) {
                        m_width = static_cast<int>(width);
                        m_height = static_cast<int>(height);
                    }
                    m_outputSubtype = subtype;
                    return true;
                }
            }
        }
        return false;
    }

    bool pushInput(const SwVideoPacket& packet) {
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

        if (packet.pts() >= 0) {
            const LONGLONG hns = static_cast<LONGLONG>((packet.pts() * 10000000LL) / 90000LL);
            sample->SetSampleTime(hns);
            sample->SetSampleDuration(0);
        }

        hr = m_decoder->ProcessInput(0, sample.Get(), 0);
        if (FAILED(hr)) {
            if (hr == MF_E_NOTACCEPTING) {
                drainOutput();
                hr = m_decoder->ProcessInput(0, sample.Get(), 0);
            }
        }
        return SUCCEEDED(hr);
    }

    void drainOutput() {
        while (true) {
            MFT_OUTPUT_STREAM_INFO info{};
            HRESULT hr = m_decoder->GetOutputStreamInfo(0, &info);
            if (FAILED(hr)) {
                return;
            }

            Microsoft::WRL::ComPtr<IMFSample> sample;
            hr = MFCreateSample(&sample);
            if (FAILED(hr)) {
                return;
            }
            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            hr = MFCreateMemoryBuffer(info.cbSize, &buffer);
            if (FAILED(hr)) {
                return;
            }
            sample->AddBuffer(buffer.Get());

            MFT_OUTPUT_DATA_BUFFER output{};
            output.pSample = sample.Get();
            DWORD status = 0;
            hr = m_decoder->ProcessOutput(0, 1, &output, &status);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                return;
            }
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                setOutputType();
                continue;
            }
            if (FAILED(hr)) {
                return;
            }
            emitFrameFromSample(sample.Get());
        }
    }

    void emitFrameFromSample(IMFSample* sample) {
        if (!sample) {
            return;
        }
        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->GetBufferByIndex(0, &buffer))) {
            return;
        }
        SwVideoFrame frame;
        if (m_outputSubtype == MFVideoFormat_NV12) {
            frame = convertNV12(buffer.Get());
        } else if (m_outputSubtype == MFVideoFormat_RGB32) {
            frame = copyBGRA(buffer.Get());
        }
        if (!frame.isValid()) {
            return;
        }
        LONGLONG ts = 0;
        if (SUCCEEDED(sample->GetSampleTime(&ts))) {
            frame.setTimestamp(static_cast<std::int64_t>(ts));
        }
        emitFrame(frame);
    }

    SwVideoFrame copyBGRA(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = width * 4;
        }

        const int rowBytes = width * 4;
        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(frame.planeData(0) + y * frame.planeStride(0),
                        data + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(rowBytes));
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    SwVideoFrame convertNV12(IMFMediaBuffer* buffer) {
        if (!buffer) {
            return {};
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        BYTE* data = nullptr;
        LONG pitch = 0;
        DWORD curLen = 0;
        bool locked2D = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            if (SUCCEEDED(buffer2D->Lock2D(&data, &pitch))) {
                locked2D = true;
            }
        }
        DWORD maxLen = 0;
        if (!locked2D) {
            if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) {
                return {};
            }
            pitch = deriveNV12Stride(static_cast<int>(curLen));
        }
        const int width = m_width > 0 ? m_width : 0;
        const int height = m_height > 0 ? m_height : 0;
        const int yStride = static_cast<int>(pitch);
        const int uvStride = static_cast<int>(pitch);
        const BYTE* yPlane = data;
        const BYTE* uvPlane = data + static_cast<size_t>(yStride) * height;

        SwVideoFrame frame = SwVideoFrame::allocate(width, height, SwVideoPixelFormat::BGRA32);
        if (!frame.isValid()) {
            if (locked2D) {
                buffer2D->Unlock2D();
            } else {
                buffer->Unlock();
            }
            return {};
        }
        auto clamp = [](int v) -> uint8_t { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
        uint8_t* dstBase = frame.planeData(0);
        const int dstStride = frame.planeStride(0);
        for (int y = 0; y < height; ++y) {
            const BYTE* yRow = yPlane + static_cast<size_t>(y) * yStride;
            const BYTE* uvRow = uvPlane + static_cast<size_t>(y / 2) * uvStride;
            uint8_t* dstRow = dstBase + static_cast<size_t>(y) * dstStride;
            for (int x = 0; x < width; ++x) {
                const uint8_t Y = yRow[x];
                const int chromaIndex = (x / 2) * 2;
                const int U = static_cast<int>(uvRow[chromaIndex]) - 128;
                const int V = static_cast<int>(uvRow[chromaIndex + 1]) - 128;
                const int C = std::max(0, static_cast<int>(Y) - 16);
                int r = (298 * C + 409 * V + 128) >> 8;
                int g = (298 * C - 100 * U - 208 * V + 128) >> 8;
                int b = (298 * C + 516 * U + 128) >> 8;
                dstRow[x * 4 + 0] = clamp(b);
                dstRow[x * 4 + 1] = clamp(g);
                dstRow[x * 4 + 2] = clamp(r);
                dstRow[x * 4 + 3] = 255;
            }
        }
        if (locked2D) {
            buffer2D->Unlock2D();
        } else {
            buffer->Unlock();
        }
        frame.setAspectRatio(1.0);
        return frame;
    }

    int deriveNV12Stride(int bufferSize) const {
        if (m_height <= 0) {
            return m_width;
        }
        const int numerator = bufferSize * 2;
        const int denominator = m_height * 3;
        if (denominator == 0) {
            return m_width;
        }
        int stride = numerator / denominator;
        return stride > 0 ? stride : m_width;
    }

    void shutdown() {
        m_decoder.Reset();
        if (m_mfStarted) {
            MFShutdown();
            m_mfStarted = false;
        }
        if (m_comInit) {
            CoUninitialize();
            m_comInit = false;
        }
        m_ready = false;
    }

    SwVideoPacket::Codec m_targetCodec;
    const char* m_name{nullptr};
    bool m_comInit{false};
    bool m_mfStarted{false};
    bool m_ready{false};
    int m_width{0};
    int m_height{0};
    GUID m_outputSubtype{GUID_NULL};
    Microsoft::WRL::ComPtr<IMFTransform> m_decoder;
};

class SwMediaFoundationH264Decoder : public SwMediaFoundationDecoderBase {
public:
    SwMediaFoundationH264Decoder()
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::H264, "SwMediaFoundationH264Decoder") {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_H264; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        return createDecoderFromEnum(MFVideoFormat_H264, decoder);
    }

    void configureInputType(IMFMediaType* type) const override {
        if (type) {
            type->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Main);
        }
    }
};

class SwMediaFoundationH265Decoder : public SwMediaFoundationDecoderBase {
public:
    SwMediaFoundationH265Decoder()
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::H265, "SwMediaFoundationH265Decoder") {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_HEVC; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        return createDecoderFromEnum(MFVideoFormat_HEVC, decoder);
    }

    void configureInputType(IMFMediaType* type) const override {
        (void)type;
    }
};

class SwMediaFoundationAv1Decoder : public SwMediaFoundationDecoderBase {
public:
    SwMediaFoundationAv1Decoder()
        : SwMediaFoundationDecoderBase(SwVideoPacket::Codec::AV1, "SwMediaFoundationAv1Decoder") {}

protected:
    GUID inputSubtype() const override { return MFVideoFormat_AV1; }

    HRESULT createDecoder(IMFTransform** decoder) const override {
        return createDecoderFromEnum(MFVideoFormat_AV1, decoder);
    }
};

// Registration helpers
static bool g_registerMFH264Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        []() { return std::make_shared<SwMediaFoundationH264Decoder>(); });
    return true;
}();

static bool g_registerMFH265Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        []() { return std::make_shared<SwMediaFoundationH265Decoder>(); });
    return true;
}();

static bool g_registerMFAv1Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        []() { return std::make_shared<SwMediaFoundationAv1Decoder>(); });
    return true;
}();

#else

// Stub for non-Windows platforms
class SwMediaFoundationH264Decoder : public SwVideoDecoder {
public:
    const char* name() const override { return "SwMediaFoundationH264DecoderStub"; }
    bool feed(const SwVideoPacket&) override { return false; }
};

class SwMediaFoundationH265Decoder : public SwVideoDecoder {
public:
    const char* name() const override { return "SwMediaFoundationH265DecoderStub"; }
    bool feed(const SwVideoPacket&) override { return false; }
};

class SwMediaFoundationAv1Decoder : public SwVideoDecoder {
public:
    const char* name() const override { return "SwMediaFoundationAv1DecoderStub"; }
    bool feed(const SwVideoPacket&) override { return false; }
};

static bool g_registerMFH264Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H264,
        []() { return std::make_shared<SwMediaFoundationH264Decoder>(); });
    return true;
}();

static bool g_registerMFH265Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::H265,
        []() { return std::make_shared<SwMediaFoundationH265Decoder>(); });
    return true;
}();

static bool g_registerMFAv1Decoder = []() {
    SwVideoDecoderFactory::instance().registerDecoder(
        SwVideoPacket::Codec::AV1,
        []() { return std::make_shared<SwMediaFoundationAv1Decoder>(); });
    return true;
}();

#endif
