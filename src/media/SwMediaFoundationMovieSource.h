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
 * @file src/media/SwMediaFoundationMovieSource.h
 * @ingroup media
 * @brief Declares the public interface exposed by SwMediaFoundationMovieSource in the CoreSw
 * media layer.
 *
 * This header belongs to the CoreSw media layer. It exposes video frames, packets, decoders,
 * capture sources, and streaming-oriented helpers used by media pipelines.
 *
 * Within that layer, this file focuses on the media foundation movie source interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * The main declarations in this header are SwMediaFoundationMovieSource.
 *
 * Source-oriented declarations here describe how data or media is produced over time, how
 * consumers observe availability, and which lifetime guarantees apply to delivered payloads.
 *
 * Media-facing declarations here focus on packet and frame ownership, format description,
 * decoding boundaries, and real-time source control.
 *
 */


/***************************************************************************************************
 * SwMediaFoundationMovieSource
 * Media Foundation backed SwVideoSource that streams frames from a file on Windows.
 *
 * On non-Windows platforms this class is a stub so the headers remain usable.
 ***************************************************************************************************/

#include "media/SwMediaTimelineSource.h"
#include "media/SwVideoSource.h"
#include "media/SwVideoPacket.h"
#include "media/SwVideoTypes.h"
#include "SwByteArray.h"
#include "SwThread.h"
#include "SwEventLoop.h"
#include "SwDebug.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include "core/fs/SwMutex.h"
#include <string>
#include <vector>
#include <iostream>
static constexpr const char* kSwLogCategory_SwMediaFoundationMovieSource = "sw.media.swmediafoundationmoviesource";


#if defined(_WIN32)

#include <wrl/client.h>
#include <combaseapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

class SwMediaFoundationMovieSource : public SwVideoSource, public SwMediaTimelineSource {
public:
    /**
     * @brief Constructs a `SwMediaFoundationMovieSource` instance.
     * @param filePath Path of the target file.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationMovieSource(const std::wstring& filePath)
        : m_path(filePath) {}

    /**
     * @brief Destroys the `SwMediaFoundationMovieSource` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwMediaFoundationMovieSource() override {
        stop();
        releaseResources();
    }

    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override {
        return "SwMediaFoundationMovieSource";
    }

    bool isSeekable() const override {
        SwMutexLocker lock(m_stateMutex);
        return m_seekable;
    }

    std::int64_t durationMs() const override {
        SwMutexLocker lock(m_stateMutex);
        return m_durationMs;
    }

    std::int64_t positionMs() const override {
        return m_lastPositionMs.load();
    }

    bool seek(std::int64_t positionMs) override {
        if (positionMs < 0) {
            positionMs = 0;
        }
        SwMutexLocker lock(m_stateMutex);
        if (!m_reader || !m_seekable) {
            return false;
        }
        PROPVARIANT target;
        PropVariantInit(&target);
        target.vt = VT_I8;
        target.hVal.QuadPart = static_cast<LONGLONG>(positionMs) * 10000LL;
        const HRESULT hr = m_reader->SetCurrentPosition(GUID_NULL, target);
        PropVariantClear(&target);
        if (FAILED(hr)) {
            logError("SetCurrentPosition", hr);
            return false;
        }
        m_havePlaybackClock = false;
        m_startTimestamp = static_cast<LONGLONG>(positionMs) * 10000LL;
        m_lastPositionMs.store(positionMs);
        emitStatus(StreamState::Streaming, "Seeking");
        return true;
    }

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
        if (m_path.empty()) {
            swCError(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] empty path.";
            return false;
        }
        if (!ensureComInitialized() || !ensureMediaFoundationStarted()) {
            return false;
        }

        Microsoft::WRL::ComPtr<IMFAttributes> readerAttributes;
        HRESULT hr = MFCreateAttributes(&readerAttributes, 2);
        if (FAILED(hr)) {
            logError("MFCreateAttributes(reader)", hr);
            return false;
        }
        readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        readerAttributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);

        hr = MFCreateSourceReaderFromURL(m_path.c_str(), readerAttributes.Get(), &m_reader);
        if (FAILED(hr)) {
            logError("MFCreateSourceReaderFromURL", hr);
            return false;
        }

        const GUID preferredSubtypes[] = {MFVideoFormat_NV12, MFVideoFormat_YUY2, MFVideoFormat_RGB32};
        bool typeSet = false;
        for (const GUID& subtype : preferredSubtypes) {
            Microsoft::WRL::ComPtr<IMFMediaType> outputType;
            hr = MFCreateMediaType(&outputType);
            if (FAILED(hr)) {
                continue;
            }
            outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            outputType->SetGUID(MF_MT_SUBTYPE, subtype);
            hr = m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.Get());
            if (SUCCEEDED(hr)) {
                typeSet = true;
                break;
            }
        }
        if (!typeSet) {
            logError("SetCurrentMediaType(all subtypes)", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> confirmedType;
        hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &confirmedType);
        if (FAILED(hr)) {
            logError("GetCurrentMediaType", hr);
            return false;
        }

        PROPVARIANT durationValue;
        PropVariantInit(&durationValue);
        hr = m_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                               MF_PD_DURATION,
                                               &durationValue);
        if (FAILED(hr)) {
            hr = m_reader->GetPresentationAttribute(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                   MF_PD_DURATION,
                                                   &durationValue);
        }
        if (SUCCEEDED(hr) &&
            (durationValue.vt == VT_I8 || durationValue.vt == VT_UI8)) {
            const LONGLONG duration100ns =
                (durationValue.vt == VT_UI8)
                    ? static_cast<LONGLONG>(durationValue.uhVal.QuadPart)
                    : durationValue.hVal.QuadPart;
            m_durationMs = duration100ns >= 0 ? (duration100ns / 10000LL) : -1;
        } else {
            m_durationMs = -1;
        }
        PropVariantClear(&durationValue);

        PROPVARIANT zeroPosition;
        PropVariantInit(&zeroPosition);
        zeroPosition.vt = VT_I8;
        zeroPosition.hVal.QuadPart = 0;
        const HRESULT seekProbeHr = m_reader->SetCurrentPosition(GUID_NULL, zeroPosition);
        PropVariantClear(&zeroPosition);
        m_seekable = SUCCEEDED(seekProbeHr);

        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(confirmedType.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr)) {
            logError("MFGetAttributeSize", hr);
            return false;
        }

        hr = confirmedType->GetGUID(MF_MT_SUBTYPE, &m_mediaSubtype);
        if (FAILED(hr)) {
            m_mediaSubtype = MFVideoFormat_RGB32;
        }

        UINT32 stride = 0;
        hr = confirmedType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride);
        if (FAILED(hr) || stride == 0) {
            LONG calcStride = 0;
            GUID strideFormat = (m_mediaSubtype == MFVideoFormat_NV12) ? MFVideoFormat_NV12
                                                                       : MFVideoFormat_RGB32;
            if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(strideFormat.Data1,
                                                         static_cast<LONG>(width),
                                                         &calcStride))) {
                stride = static_cast<UINT32>(calcStride);
            } else if (m_mediaSubtype == MFVideoFormat_NV12) {
                stride = width;
            } else {
                stride = width * 4;
            }
        }

        auto subtypeLabel = [this]() -> const char* {
            if (m_mediaSubtype == MFVideoFormat_NV12) {
                return "NV12";
            }
            if (m_mediaSubtype == MFVideoFormat_YUY2) {
                return "YUY2";
            }
            return "RGB/BGRA";
        };
        swCDebug(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] Confirmed type: " << subtypeLabel()
                  << " width=" << width << " height=" << height << " stride=" << stride;

        m_frameWidth = static_cast<int>(width);
        m_frameHeight = static_cast<int>(height);
        m_defaultStride = static_cast<int>(stride);
        m_initialized = true;
        publishTracks_();
        return true;
    }

    /**
     * @brief Starts the underlying activity managed by the object.
     *
     * @details The call affects the runtime state associated with the underlying resource or service.
     */
    void start() override {
        if (isRunning()) {
            return;
        }
        emitStatus(StreamState::Connecting, "Opening media file...");
        if (!initialize()) {
            emitStatus(StreamState::Recovering, "Failed to initialize movie source");
            return;
        }
        m_havePlaybackClock = false;
        m_lastPositionMs.store(0);
        setRunning(true);
        if (!m_captureThread) {
            m_captureThread = std::make_unique<MovieCaptureThread>(this);
        }
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
        if (m_captureThread) {
            m_captureThread->quit();
            m_captureThread->wait();
            m_captureThread.reset();
        }
    }

    /**
     * @brief Sets the loop.
     * @param loop Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setLoop(bool loop) {
        m_loop.store(loop);
    }

private:
    void publishTracks_() {
        SwMediaTrack track;
        track.id = "file-video-0";
        track.type = SwMediaTrack::Type::Video;
        track.codec = "raw-bgra";
        track.selected = true;
        track.availability = SwMediaTrack::Availability::Available;
        SwList<SwMediaTrack> tracks;
        tracks.append(track);
        setTracks(tracks);
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

    void readLoop() {
        bool reachedEndOfFile = false;
        while (isRunning()) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            Microsoft::WRL::ComPtr<IMFSample> sample;
            HRESULT hr = S_OK;
            {
                SwMutexLocker lock(m_stateMutex);
                if (!m_reader) {
                    break;
                }
                hr = m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                          0,
                                          &streamIndex,
                                          &flags,
                                          &timestamp,
                                          &sample);
            }
            if (FAILED(hr)) {
                logError("ReadSample", hr);
                emitStatus(StreamState::Recovering, "Movie read failed");
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (m_loop.load()) {
                    PROPVARIANT loopTarget;
                    PropVariantInit(&loopTarget);
                    {
                        SwMutexLocker lock(m_stateMutex);
                        if (m_reader) {
                            m_reader->SetCurrentPosition(GUID_NULL, loopTarget);
                        }
                    }
                    PropVariantClear(&loopTarget);
                    m_havePlaybackClock = false;
                    m_lastPositionMs.store(0);
                    continue;
                }
                reachedEndOfFile = true;
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

            SwByteArray payload;
            int actualHeight = m_frameHeight;
            if (!copyFrame(buffer.Get(), payload, actualHeight)) {
                continue;
            }
            if (actualHeight > 0 && actualHeight != m_frameHeight) {
                swCWarning(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] Adjusting visible height from "
                            << m_frameHeight << " to " << actualHeight;
                m_frameHeight = actualHeight;
            }

            if (!m_havePlaybackClock) {
                m_playbackStart = std::chrono::steady_clock::now();
                m_startTimestamp = timestamp;
                m_havePlaybackClock = true;
            }
            LONGLONG relativePts = timestamp - m_startTimestamp;
            if (relativePts < 0) {
                relativePts = 0;
            }
            auto dueTime = m_playbackStart + std::chrono::nanoseconds(relativePts * 100);
            auto now = std::chrono::steady_clock::now();
            if (dueTime > now) {
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(dueTime - now).count();
                if (diff > 0) {
                    SwEventLoop::swsleep(static_cast<int>(diff));
                }
            }

            SwVideoFormatInfo format =
                SwDescribeVideoFormat(SwVideoPixelFormat::BGRA32, m_frameWidth, m_frameHeight);
            if (actualHeight > 0) {
                format.height = actualHeight;
                format.planeHeights[0] = actualHeight;
            }
            format.dataSize = static_cast<std::size_t>(payload.size());
            SwVideoPacket packet(SwVideoPacket::Codec::RawBGRA,
                                 std::move(payload),
                                 timestamp,
                                 timestamp,
                                 true);
            packet.setRawFormat(format);
            m_lastPositionMs.store(timestamp >= 0 ? (timestamp / 10000LL) : -1);
            emitStatus(StreamState::Streaming, "Streaming");
            emitPacket(packet);
        }
        setRunning(false);
        emitStatus(StreamState::Stopped,
                   reachedEndOfFile ? SwString("End of file") : SwString("Stream stopped"));
    }

    bool copyFrame(IMFMediaBuffer* buffer, SwByteArray& out, int& outHeight) const {
        if (!buffer || m_frameWidth <= 0 || m_frameHeight <= 0) {
            return false;
        }

        Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
        DWORD currentLength = 0;
        buffer->GetCurrentLength(&currentLength);
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) {
            BYTE* scanline = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(buffer2D->Lock2D(&scanline, &pitch))) {
                bool ok = copyFromPointer(scanline, pitch, static_cast<int>(currentLength), out, outHeight);
                if (ok) {
                    swCDebug(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] copyFrame(2D) pitch=" << pitch
                              << " rows=" << outHeight << " size=" << out.size();
                }
                buffer2D->Unlock2D();
                return ok;
            }
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        HRESULT hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            return false;
        }
        LONG stride = m_defaultStride;
        if (m_mediaSubtype == MFVideoFormat_NV12) {
            if (stride <= 0) {
                stride = deriveNV12Stride(static_cast<int>(currentLength));
            }
            bool ok = convertNV12Pointer(data, stride, static_cast<int>(currentLength), out, outHeight);
            if (ok) {
                swCDebug(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] copyFrame(NV12) stride=" << stride
                          << " rows=" << outHeight << " size=" << out.size();
            }
            buffer->Unlock();
            return ok;
        } else if (stride <= 0) {
            stride = m_frameWidth * 4;
        }
        bool ok = copyFromPointer(data, stride, static_cast<int>(currentLength), out, outHeight);
        if (ok) {
            swCDebug(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] copyFrame(RGB) stride=" << stride
                      << " rows=" << outHeight << " size=" << out.size();
        }
        buffer->Unlock();
        return ok;
    }

    bool copyFromPointer(const BYTE* src, LONG pitch, int bufferLength, SwByteArray& out, int& rowsCopied) const {
        if (m_mediaSubtype == MFVideoFormat_NV12) {
            return convertNV12Pointer(src, pitch, bufferLength, out, rowsCopied);
        } else if (m_mediaSubtype == MFVideoFormat_YUY2) {
            return convertYUY2Pointer(src, pitch, bufferLength, out, rowsCopied);
        }
        return copyBGRAFromPointer(src, pitch, bufferLength, out, rowsCopied);
    }

    bool copyBGRAFromPointer(const BYTE* src, LONG pitch, int bufferLength, SwByteArray& out, int& rowsCopied) const {
        if (!src) {
            return false;
        }
        const int rowBytes = m_frameWidth * 4;
        const LONG absPitch = pitch < 0 ? -pitch : pitch;
        const int rowsAvailable = (absPitch > 0 && bufferLength > 0) ? bufferLength / absPitch : m_frameHeight;
        rowsCopied = (std::max)(1, (std::min)(m_frameHeight, rowsAvailable));
        out = SwByteArray(static_cast<std::size_t>(rowBytes) * rowsCopied, '\0');
        char* dst = out.data();
        if (!dst) {
            return false;
        }

        const int bytesPerRowToCopy = (std::min)(rowBytes, static_cast<int>(absPitch));
        if (bytesPerRowToCopy < rowBytes) {
            swCWarning(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] row bytes (" << rowBytes
                        << ") exceed pitch (" << absPitch << "), padding with zeros.";
        }
        for (int y = 0; y < rowsCopied; ++y) {
            const BYTE* srcRow =
                pitch >= 0 ? src + static_cast<std::size_t>(y) * absPitch
                           : src + static_cast<std::size_t>(rowsCopied - 1 - y) * absPitch;
            uint8_t* dstRow = reinterpret_cast<uint8_t*>(dst + static_cast<std::size_t>(y) * rowBytes);
            std::memcpy(dstRow, srcRow, bytesPerRowToCopy);
            if (bytesPerRowToCopy < rowBytes) {
                std::memset(dstRow + bytesPerRowToCopy, 0, rowBytes - bytesPerRowToCopy);
            }
        }
        return true;
    }

    bool convertNV12Pointer(const BYTE* yPlane, LONG pitchY, int bufferLength, SwByteArray& out, int& rowsCopied) const {
        if (!yPlane) {
            return false;
        }
        const int width = m_frameWidth;
        const LONG absPitch = pitchY < 0 ? -pitchY : pitchY;
        const int storedRows =
            (absPitch > 0 && bufferLength > 0)
                ? static_cast<int>((static_cast<long long>(bufferLength) * 2) /
                                   (static_cast<long long>(absPitch) * 3))
                : m_frameHeight;
        const int clampedStoredRows = storedRows > 0 ? storedRows : m_frameHeight;
        rowsCopied = (std::max)(1, (std::min)(m_frameHeight, clampedStoredRows));
        const BYTE* uvPlane =
            (pitchY >= 0) ? yPlane + static_cast<std::size_t>(absPitch) * clampedStoredRows
                          : yPlane - static_cast<std::size_t>(absPitch) * clampedStoredRows;

        out = SwByteArray(static_cast<std::size_t>(width) * rowsCopied * 4, '\0');
        char* dst = out.data();
        if (!dst) {
            return false;
        }

        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>((std::max)(0, (std::min)(255, v)));
        };

        for (int y = 0; y < rowsCopied; ++y) {
            const BYTE* yRow =
                (pitchY >= 0) ? yPlane + static_cast<std::size_t>(y) * absPitch
                              : yPlane - static_cast<std::size_t>(y) * absPitch;
            const BYTE* uvRow =
                (pitchY >= 0) ? uvPlane + static_cast<std::size_t>(y / 2) * absPitch
                              : uvPlane - static_cast<std::size_t>(y / 2) * absPitch;
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

    bool convertYUY2Pointer(const BYTE* src, LONG pitch, int bufferLength, SwByteArray& out, int& rowsCopied) const {
        if (!src) {
            return false;
        }
        const LONG absPitch = pitch < 0 ? -pitch : pitch;
        const int srcRowBytes = m_frameWidth * 2;
        const int rowsAvailable = (absPitch > 0 && bufferLength > 0) ? bufferLength / absPitch : m_frameHeight;
        rowsCopied = (std::max)(1, (std::min)(m_frameHeight, rowsAvailable));

        out = SwByteArray(static_cast<std::size_t>(m_frameWidth) * rowsCopied * 4, '\0');
        char* dst = out.data();
        if (!dst) {
            return false;
        }

        auto clamp = [](int v) -> uint8_t {
            return static_cast<uint8_t>((std::max)(0, (std::min)(255, v)));
        };

        const int bytesPerRow = (std::min)(static_cast<int>(absPitch), srcRowBytes);
        for (int y = 0; y < rowsCopied; ++y) {
            const BYTE* srcRow =
                pitch >= 0 ? src + static_cast<std::size_t>(y) * absPitch
                           : src + static_cast<std::size_t>(rowsCopied - 1 - y) * absPitch;
            uint8_t* dstRow = reinterpret_cast<uint8_t*>(dst + static_cast<std::size_t>(y) * m_frameWidth * 4);
            for (int x = 0; x < m_frameWidth; x += 2) {
                const int idx = x * 2;
                if (idx + 3 >= bytesPerRow) {
                    break;
                }
                const int y0 = srcRow[idx + 0];
                const int u = srcRow[idx + 1] - 128;
                const int y1 = srcRow[idx + 2];
                const int v = srcRow[idx + 3] - 128;
                const int c0 = std::max(0, y0 - 16);
                const int c1 = std::max(0, y1 - 16);

                auto convertPixel = [&](int C, uint8_t* dstPixel) {
                    int r = (298 * C + 409 * v + 128) >> 8;
                    int g = (298 * C - 100 * u - 208 * v + 128) >> 8;
                    int b = (298 * C + 516 * u + 128) >> 8;
                    dstPixel[0] = clamp(b);
                    dstPixel[1] = clamp(g);
                    dstPixel[2] = clamp(r);
                    dstPixel[3] = 255;
                };

                convertPixel(c0, dstRow + x * 4);
                if (x + 1 < m_frameWidth) {
                    convertPixel(c1, dstRow + (x + 1) * 4);
                }
            }
        }
        return true;
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

    void releaseResources() {
        m_reader.Reset();
        if (m_mfStarted) {
            MFShutdown();
            m_mfStarted = false;
        }
        if (m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
        m_initialized = false;
        m_seekable = false;
        m_durationMs = -1;
        m_lastPositionMs.store(-1);
    }

    void logError(const char* label, HRESULT hr) const {
        swCError(kSwLogCategory_SwMediaFoundationMovieSource) << "[SwMediaFoundationMovieSource] " << label << " failed: 0x" << std::hex << hr << std::dec;
    }

    class MovieCaptureThread : public SwThread {
    public:
        /**
         * @brief Constructs a `MovieCaptureThread` instance.
         * @param owner Value passed to the method.
         *
         * @details The instance is initialized and prepared for immediate use.
         */
        explicit MovieCaptureThread(SwMediaFoundationMovieSource* owner)
            : SwThread("SwMediaFoundationMovieSourceThread"), m_owner(owner) {}

    protected:
        /**
         * @brief Performs the `run` operation.
         */
        void run() override {
            if (m_owner) {
                m_owner->readLoop();
            }
        }

    private:
        SwMediaFoundationMovieSource* m_owner{nullptr};
    };

    std::wstring m_path;
    Microsoft::WRL::ComPtr<IMFSourceReader> m_reader;
    std::unique_ptr<MovieCaptureThread> m_captureThread;
    mutable SwMutex m_stateMutex;
    int m_frameWidth{0};
    int m_frameHeight{0};
    int m_defaultStride{0};
    GUID m_mediaSubtype{MFVideoFormat_RGB32};
    bool m_initialized{false};
    bool m_comInitialized{false};
    bool m_mfStarted{false};
    bool m_seekable{false};
    std::atomic<bool> m_loop{false};
    std::atomic<std::int64_t> m_lastPositionMs{-1};
    bool m_havePlaybackClock{false};
    std::int64_t m_durationMs{-1};
    std::chrono::steady_clock::time_point m_playbackStart{};
    LONGLONG m_startTimestamp{0};
};

#else

class SwMediaFoundationMovieSource : public SwVideoSource, public SwMediaTimelineSource {
public:
    /**
     * @brief Constructs a `SwMediaFoundationMovieSource` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    explicit SwMediaFoundationMovieSource(const std::wstring&) {}
    /**
     * @brief Returns the current name.
     * @return The current name.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    SwString name() const override { return "SwMediaFoundationMovieSourceStub"; }
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

    bool isSeekable() const override { return false; }
    std::int64_t durationMs() const override { return -1; }
    std::int64_t positionMs() const override { return -1; }
    bool seek(std::int64_t) override { return false; }
};

#endif
