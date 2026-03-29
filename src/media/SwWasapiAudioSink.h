#pragma once

/**
 * @file src/media/SwWasapiAudioSink.h
 * @ingroup media
 * @brief Declares a Windows WASAPI-backed audio sink used by SwAudioOutput.
 */

#include "media/SwAudioSink.h"

#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "ole32.lib")

class SwWasapiAudioSink : public SwAudioSink {
public:
    SwWasapiAudioSink() = default;

    ~SwWasapiAudioSink() override {
        close();
    }

    const char* name() const override { return "SwWasapiAudioSink"; }

    bool open(int sampleRate, int channelCount) override {
        close();
        if (sampleRate <= 0 || channelCount <= 0) {
            return false;
        }

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                              nullptr,
                              CLSCTX_ALL,
                              IID_PPV_ARGS(enumerator.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, m_device.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }

        hr = m_device->Activate(__uuidof(IAudioClient),
                                CLSCTX_ALL,
                                nullptr,
                                reinterpret_cast<void**>(m_audioClient.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = static_cast<WORD>(channelCount);
        format.Format.nSamplesPerSec = static_cast<DWORD>(sampleRate);
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign =
            static_cast<WORD>(format.Format.nChannels * (format.Format.wBitsPerSample / 8));
        format.Format.nAvgBytesPerSec =
            format.Format.nSamplesPerSec * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = (channelCount == 1) ? SPEAKER_FRONT_CENTER
                                                   : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
        format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

        REFERENCE_TIME bufferDuration = 200000; // 20 ms
        hr = m_audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                           AUDCLNT_STREAMFLAGS_NOPERSIST,
                                       bufferDuration,
                                       0,
                                       reinterpret_cast<WAVEFORMATEX*>(&format),
                                       nullptr);
        if (FAILED(hr)) {
            return false;
        }

        hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
        if (FAILED(hr)) {
            return false;
        }

        hr = m_audioClient->GetService(IID_PPV_ARGS(m_renderClient.GetAddressOf()));
        if (FAILED(hr)) {
            return false;
        }

        m_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!m_event) {
            return false;
        }

        hr = m_audioClient->SetEventHandle(m_event.get());
        if (FAILED(hr)) {
            return false;
        }

        m_sampleRate = sampleRate;
        m_channelCount = channelCount;
        m_bytesPerFrame = static_cast<std::size_t>(channelCount * sizeof(float));
        m_stopRequested.store(false);
        m_worker = std::thread([this]() { workerLoop_(); });

        hr = m_audioClient->Start();
        if (FAILED(hr)) {
            close();
            return false;
        }
        m_open = true;
        return true;
    }

    void close() override {
        m_stopRequested.store(true);
        if (m_event) {
            SetEvent(m_event.get());
        }
        m_queueCv.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        if (m_audioClient) {
            m_audioClient->Stop();
        }
        m_renderClient.Reset();
        m_audioClient.Reset();
        m_device.Reset();
        m_event.reset();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_chunks.clear();
        }
        m_open = false;
        m_bufferFrameCount = 0;
        m_sampleRate = 0;
        m_channelCount = 0;
        m_bytesPerFrame = 0;
        m_lastPlayedTimestamp.store(-1);
    }

    bool pushFrame(const SwAudioFrame& frame) override {
        if (!m_open || !frame.isValid() || frame.sampleFormat() != SwAudioFrame::SampleFormat::Float32) {
            return false;
        }
        if (frame.sampleRate() != m_sampleRate || frame.channelCount() != m_channelCount) {
            return false;
        }
        QueuedChunk chunk;
        chunk.payload = frame.payload();
        chunk.timestamp = frame.timestamp();
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_chunks.push_back(std::move(chunk));
        }
        m_queueCv.notify_one();
        if (m_event) {
            SetEvent(m_event.get());
        }
        return true;
    }

    std::int64_t playedTimestamp() const override {
        return m_lastPlayedTimestamp.load();
    }

private:
    struct QueuedChunk {
        SwByteArray payload{};
        std::size_t offset{0};
        std::int64_t timestamp{-1};
    };

    struct HandleCloser {
        void operator()(HANDLE handle) const {
            if (handle) {
                CloseHandle(handle);
            }
        }
    };

    void workerLoop_() {
        while (!m_stopRequested.load()) {
            if (!m_audioClient || !m_renderClient) {
                break;
            }

            HANDLE waitHandle = m_event.get();
            if (!waitHandle) {
                break;
            }
            DWORD waitResult = WaitForSingleObject(waitHandle, 50);
            if (m_stopRequested.load()) {
                break;
            }
            if (waitResult != WAIT_OBJECT_0 && waitResult != WAIT_TIMEOUT) {
                continue;
            }

            UINT32 padding = 0;
            if (FAILED(m_audioClient->GetCurrentPadding(&padding))) {
                continue;
            }
            if (padding >= m_bufferFrameCount) {
                continue;
            }
            const UINT32 framesToWrite = m_bufferFrameCount - padding;
            BYTE* buffer = nullptr;
            if (FAILED(m_renderClient->GetBuffer(framesToWrite, &buffer)) || !buffer) {
                continue;
            }

            const std::size_t bytesToWrite =
                static_cast<std::size_t>(framesToWrite) * m_bytesPerFrame;
            std::vector<char> temp(bytesToWrite, 0);
            fillBuffer_(temp.data(), bytesToWrite);
            std::memcpy(buffer, temp.data(), bytesToWrite);
            m_renderClient->ReleaseBuffer(framesToWrite, 0);
        }
    }

    void fillBuffer_(char* dst, std::size_t byteCount) {
        if (!dst || byteCount == 0) {
            return;
        }
        std::size_t written = 0;
        std::lock_guard<std::mutex> lock(m_queueMutex);
        while (written < byteCount && !m_chunks.empty()) {
            auto& chunk = m_chunks.front();
            const std::size_t remaining = static_cast<std::size_t>(chunk.payload.size()) - chunk.offset;
            if (remaining == 0) {
                m_lastPlayedTimestamp.store(chunk.timestamp);
                m_chunks.pop_front();
                continue;
            }
            const std::size_t copySize = std::min(remaining, byteCount - written);
            std::memcpy(dst + written, chunk.payload.constData() + chunk.offset, copySize);
            chunk.offset += copySize;
            written += copySize;
            if (chunk.offset >= static_cast<std::size_t>(chunk.payload.size())) {
                m_lastPlayedTimestamp.store(chunk.timestamp);
                m_chunks.pop_front();
            }
        }
        if (written < byteCount) {
            std::memset(dst + written, 0, byteCount - written);
        }
    }

    bool m_comInitialized{false};
    std::atomic<bool> m_stopRequested{false};
    bool m_open{false};
    int m_sampleRate{0};
    int m_channelCount{0};
    std::size_t m_bytesPerFrame{0};
    UINT32 m_bufferFrameCount{0};
    Microsoft::WRL::ComPtr<IMMDevice> m_device;
    Microsoft::WRL::ComPtr<IAudioClient> m_audioClient;
    Microsoft::WRL::ComPtr<IAudioRenderClient> m_renderClient;
    std::unique_ptr<void, HandleCloser> m_event;
    std::thread m_worker;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<QueuedChunk> m_chunks;
    std::atomic<std::int64_t> m_lastPlayedTimestamp{-1};
};
#endif
