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

/**
 * @file src/media/SwMediaFoundationVideoSource.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwMediaFoundationVideoSource in the CoreSw
 * media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the media foundation video source interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwMediaFoundationVideoSource.
 *
 * Source-oriented declarations here describe how data or media is produced over time, how
 * consumers observe availability, and which lifetime guarantees apply to delivered payloads.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/***************************************************************************************************
 * Windows-only video source that captures frames via Media Foundation and emits BGRA SwVideoPacket.
 * The API stays header-only so higher layers (widgets/examples) only hook up the source and start it.
 ***************************************************************************************************/

#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "SwThread.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include "core/fs/SwMutex.h"
static constexpr const char* kSwLogCategory_SwMediaFoundationVideoSource = "sw.media.swmediafoundationvideosource";


#if defined(_WIN32)

#include <combaseapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

class SwMediaFoundationVideoSource : public SwVideoSource {
public:
    /**
     * @brief Constructs a `SwMediaFoundationVideoSource` instance.
     * @param deviceIndex Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationVideoSource(UINT32 deviceIndex = 0)
        : m_deviceIndex(deviceIndex) {}

    /**
     * @brief Destroys the `SwMediaFoundationVideoSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwMediaFoundationVideoSource() override {
        stop();
        releaseResources();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override { return "SwMediaFoundationVideoSource"; }

    /**
     * @brief Returns the current initialize.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool initialize() {
        SwMutexLocker lock(m_stateMutex);
        if (m_initialized) {
            return true;
        }
        if (!ensureComInitialized() || !ensureMediaFoundationStarted()) {
            return false;
        }

        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 1);
        if (FAILED(hr)) {
            logError("MFCreateAttributes", hr);
            return false;
        }
        hr = attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                 MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        if (FAILED(hr)) {
            logError("SetGUID", hr);
            return false;
        }

        IMFActivate** devices = nullptr;
        UINT32 deviceCount = 0;
        hr = MFEnumDeviceSources(attributes.Get(), &devices, &deviceCount);
        if (FAILED(hr) || deviceCount == 0) {
            swCError(kSwLogCategory_SwMediaFoundationVideoSource) << "[SwMediaFoundationVideoSource] No video capture devices found.";
            if (devices) {
                CoTaskMemFree(devices);
            }
            return false;
        }
        UINT32 index = (m_deviceIndex >= deviceCount) ? deviceCount - 1 : m_deviceIndex;
        hr = devices[index]->ActivateObject(IID_PPV_ARGS(&m_mediaSource));
        for (UINT32 i = 0; i < deviceCount; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        if (FAILED(hr)) {
            logError("ActivateObject", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IMFAttributes> readerAttributes;
        hr = MFCreateAttributes(&readerAttributes, 2);
        if (FAILED(hr)) {
            logError("MFCreateAttributes(reader)", hr);
            return false;
        }
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        readerAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);

        hr = MFCreateSourceReaderFromMediaSource(m_mediaSource.Get(), readerAttributes.Get(), &m_reader);
        if (FAILED(hr)) {
            logError("MFCreateSourceReaderFromMediaSource", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> targetType;
        hr = MFCreateMediaType(&targetType);
        if (FAILED(hr)) {
            logError("MFCreateMediaType", hr);
            return false;
        }
        targetType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        targetType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, targetType.Get());
        if (FAILED(hr)) {
            logError("SetCurrentMediaType", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> confirmedType;
        hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &confirmedType);
        if (FAILED(hr)) {
            logError("GetCurrentMediaType", hr);
            return false;
        }

        hr = confirmedType->GetGUID(MF_MT_SUBTYPE, &m_mediaSubtype);
        if (FAILED(hr)) {
            m_mediaSubtype = MFVideoFormat_RGB32;
        }
        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(confirmedType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) {
            logError("MFGetAttributeSize", hr);
            return false;
        }

        UINT32 stride = 0;
        hr = confirmedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride);
        if (FAILED(hr) || stride == 0) {
            LONG calcStride = 0;
            GUID strideFormat;
            if (m_mediaSubtype == MFVideoFormat_NV12) {
                strideFormat = MFVideoFormat_NV12;
            } else {
                strideFormat = MFVideoFormat_RGB32;
            }
            if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(strideFormat.Data1,
                                                         static_cast<LONG>(width),
                                                         &calcStride))) {
                stride = static_cast<UINT32>(calcStride);
            } else {
                stride = width * (m_mediaSubtype == MFVideoFormat_NV12 ? 1U : 4U);
            }
        }

        m_frameWidth = static_cast<int>(width);
        m_frameHeight = static_cast<int>(height);
        m_defaultStride = static_cast<int>(stride);
        m_initialized = true;
        return true;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {
        if (isRunning() || !m_reader || !initialize()) {
            return;
        }
        if (!m_captureThread) {
            m_captureThread = std::make_unique<CaptureThread>(this);
        }
        setRunning(true);
        m_captureThread->start();
    }

    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() override {
        if (!isRunning()) {
            return;
        }
        setRunning(false);
        if (m_reader) {
            m_reader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
        }
        if (m_captureThread) {
            m_captureThread->quit();
            m_captureThread->wait();
            m_captureThread.reset();
        }
    }

private:
    void logError(const char* label, HRESULT hr) const {
        swCError(kSwLogCategory_SwMediaFoundationVideoSource) << "[SwMediaFoundationVideoSource] " << label << " failed: 0x" << std::hex << hr << std::dec;
    }

    bool ensureComInitialized() {
        if (m_comInitialized) {
            return true;
        }
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (hr == RPC_E_CHANGED_MODE) {
            m_comInitialized = true;
            return true;
        }
        if (FAILED(hr)) {
            logError("CoInitializeEx", hr);
            return false;
        }
        m_comInitialized = true;
        return true;
    }

    bool ensureMediaFoundationStarted() {
        if (m_mfStarted) {
            return true;
        }
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
        if (FAILED(hr)) {
            logError("MFStartup", hr);
            return false;
        }
        m_mfStarted = true;
        return true;
    }

    void captureLoop() {
        while (isRunning()) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            Microsoft::WRL::ComPtr<IMFSample> sample;
            HRESULT hr = m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              0,
                                              &streamIndex,
                                              &flags,
                                              &timestamp,
                                              &sample);

            if (FAILED(hr)) {
                logError("ReadSample", hr);
                break;
            }
            if (!isRunning() || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                break;
            }
            if (!sample) {
                continue;
            }

            Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
            hr = sample->GetBufferByIndex(0, &buffer);
            if (FAILED(hr)) {
                hr = sample->ConvertToContiguousBuffer(&buffer);
                if (FAILED(hr)) {
                    continue;
                }
            }

            SwVideoPacket packet;
            if (m_mediaSubtype == MFVideoFormat_NV12) {
                SwByteArray payload;
                if (!convertNV12ToBGRA(buffer.Get(), payload)) {
                    continue;
                }
                SwVideoFormatInfo format =
                    SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, m_frameWidth, m_frameHeight);
                packet = SwVideoPacket(SwVideoPacket::Codec::RawBGRA,
                                       std::move(payload),
                                       timestamp,
                                       timestamp,
                                       true);
                packet.setRawFormat(format);
            } else {
                SwByteArray payload;
                if (!copyBGRAFromBuffer(buffer.Get(), payload)) {
                    continue;
                }
                SwVideoFormatInfo format =
                    SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, m_frameWidth, m_frameHeight);
                packet = SwVideoPacket(SwVideoPacket::Codec::RawBGRA,
                                       std::move(payload),
                                       timestamp,
                                       timestamp,
                                       true);
                packet.setRawFormat(format);
            }
            emitPacket(packet);
        }
    }

    bool copyBGRAFromBuffer(IMFMediaBuffer* buffer, SwByteArray& out) const {
        if (!buffer || m_frameWidth <= 0 || m_frameHeight <= 0) {
            return false;
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            BYTE* scanline = nullptr;
            LONG pitch = 0;
            if (FAILED(buffer2D->Lock2D(&scanline, &pitch))) {
                return false;
            }
            bool ok = copyBGRAFromPointer(scanline, pitch, out);
            buffer2D->Unlock2D();
            return ok;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        HRESULT hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            return false;
        }
        const LONG stride = (m_defaultStride > 0) ? m_defaultStride : m_frameWidth * 4;
        bool ok = copyBGRAFromPointer(data, stride, out);
        buffer->Unlock();
        return ok;
    }

    bool convertNV12ToBGRA(IMFMediaBuffer* buffer, SwByteArray& out) const {
        if (!buffer || m_frameWidth <= 0 || m_frameHeight <= 0) {
            return false;
        }
        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            BYTE* yPlane = nullptr;
            LONG pitch = 0;
            if (FAILED(buffer2D->Lock2D(&yPlane, &pitch))) {
                return false;
            }
            bool ok = convertNV12Pointer(yPlane, pitch, out);
            buffer2D->Unlock2D();
            return ok;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        HRESULT hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            return false;
        }
        const int stride = (m_defaultStride > 0) ? m_defaultStride : deriveNV12Stride(static_cast<int>(currentLength));
        bool ok = convertNV12Pointer(data, stride, out);
        buffer->Unlock();
        return ok;
    }

    int deriveNV12Stride(int bufferSize) const {
        if (m_frameHeight <= 0) {
            return m_frameWidth;
        }
        const int numerator = bufferSize * 2;
        const int denominator = m_frameHeight * 3;
        if (denominator == 0) {
            return m_frameWidth;
        }
        int stride = numerator / denominator;
        return stride > 0 ? stride : m_frameWidth;
    }

    bool copyBGRAFromPointer(const BYTE* src, LONG pitch, SwByteArray& out) const {
        if (!src) {
            return false;
        }
        const int rowBytes = m_frameWidth * 4;
        out = SwByteArray(static_cast<std::size_t>(rowBytes) * m_frameHeight, '\0');
        char* dst = out.data();
        if (!dst) {
            return false;
        }
        const LONG absPitch = pitch < 0 ? -pitch : pitch;
        for (int y = 0; y < m_frameHeight; ++y) {
            const BYTE* srcRow =
                pitch >= 0 ? src + static_cast<std::size_t>(y) * absPitch
                           : src + static_cast<std::size_t>(m_frameHeight - 1 - y) * absPitch;
            std::memcpy(dst + static_cast<std::size_t>(y) * rowBytes, srcRow, rowBytes);
        }
        return true;
    }

    bool convertNV12Pointer(const BYTE* yPlane, LONG pitchY, SwByteArray& out) const {
        if (!yPlane) {
            return false;
        }
        const int width = m_frameWidth;
        const int height = m_frameHeight;
        const BYTE* uvPlane = yPlane + static_cast<std::size_t>(pitchY) * height;

        out = SwByteArray(static_cast<std::size_t>(width) * height * 4, '\0');
        char* dst = out.data();
        if (!dst) {
            return false;
        }

        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>(std::max(0, std::min(255, v)));
        };

        for (int y = 0; y < height; ++y) {
            const BYTE* yRow = yPlane + static_cast<std::size_t>(y) * pitchY;
            const BYTE* uvRow = uvPlane + static_cast<std::size_t>(y / 2) * pitchY;
            uint8_t* dstRow = reinterpret_cast<uint8_t*>(dst + static_cast<std::size_t>(y) * width * 4);
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
        return true;
    }

    void releaseResources() {
        m_reader.Reset();
        m_mediaSource.Reset();
        if (m_mfStarted) {
            MFShutdown();
            m_mfStarted = false;
        }
        if (m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
        m_initialized = false;
    }

    UINT32 m_deviceIndex{0};
    Microsoft::WRL::ComPtr<IMFMediaSource> m_mediaSource;
    Microsoft::WRL::ComPtr<IMFSourceReader> m_reader;
    class CaptureThread : public SwThread {
    public:
        /**
         * @brief Constructs a `CaptureThread` instance.
         * @param owner Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit CaptureThread(SwMediaFoundationVideoSource* owner)
            : SwThread("SwMediaFoundationVideoSourceThread"), m_owner(owner) {}

    protected:
        /**
         * @brief Performs the `run` operation.
         */
        void run() override {
            if (m_owner) {
                m_owner->captureLoop();
            }
        }

    private:
        SwMediaFoundationVideoSource* m_owner{nullptr};
    };
    std::unique_ptr<CaptureThread> m_captureThread;
    SwMutex m_stateMutex;
    int m_frameWidth{0};
    int m_frameHeight{0};
    bool m_initialized{false};
    bool m_comInitialized{false};
    bool m_mfStarted{false};
    int m_defaultStride{0};
    GUID m_mediaSubtype{MFVideoFormat_RGB32};
};

#else

// Stub version for non-Windows platforms: the source can't run but keeps API surface.
class SwMediaFoundationVideoSource : public SwVideoSource {
public:
    /**
     * @brief Constructs a `SwMediaFoundationVideoSource` instance.
     * @param UINT32 Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationVideoSource(UINT32 /*deviceIndex*/ = 0) {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override { return "SwMediaFoundationVideoSourceStub"; }
    /**
     * @brief Returns the current initialize.
     * @return `true` on success; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool initialize() { return false; }
    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {}
    /**
     * @brief Stops the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void stop() override {}
};

#endif
